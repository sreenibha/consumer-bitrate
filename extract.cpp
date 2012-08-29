#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "extract.hpp"

#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

bool keep_running = true;
extern const char* program_name;

static double my_round (double value){
	static const double bias = 0.0005;
	return (floor(value + bias));
}

static double roundtwo (double value){
	static const double bias = 0.0005;
	return (floor (value + bias));
}

static int prefix_to_multiplier(char prefix){
	prefix = tolower(prefix);
	switch ( prefix ){
	case 0: return 1;
	case 'k': return 1e3;
	case 'm': return 1e6;
	case 'g': return 1e9;
	default: return -1;
	}
}

/**
 * Get prefix from number represented by a string and removes the prefix by
 * setting it to NULL.
 * If no prefix was found it returns 0.
 * E.g. "100k" -> 'k'.
 */
static char pop_prefix(char* string){
	if ( *string == 0 ) return 0;

	const size_t offset = strlen(string) - 1;
	if ( ! isalpha(string[offset]) ){
		return 0;
	}

	const char prefix = string[offset];
	string[offset] = 0;
	return prefix;
}

Extractor::Extractor()
	: first_packet(true)
	, max_packets(0)
	, level(0)
	, bits(0) {

	set_sampling_frequency(1.0); /* default to 1Hz */
	set_link_capacity("100m");   /* default to 100mbps */
}

Extractor::~Extractor(){

}

void Extractor::set_sampling_frequency(double hz){
	sampleFrequency = hz;
	tSample = 1.0 / sampleFrequency;
}

void Extractor::set_sampling_frequency(const char* str){
	char* tmp = strdup(str);
	const char prefix = pop_prefix(tmp);
	int multiplier = prefix_to_multiplier(prefix);

	if ( multiplier == -1 ){
		fprintf(stderr, "unknown prefix '%c' for --sampleFrequency, ignored.\n", prefix);
		multiplier = 1;
	}

	set_sampling_frequency(atof(tmp) * multiplier);
	free(tmp);
}

double Extractor::get_sampling_frequency() const {
	return sampleFrequency;
}

void Extractor::set_max_packets(size_t n){
	max_packets = n;
}

void Extractor::set_link_capacity(unsigned long bps){
	link_capacity = bps;
}

void Extractor::set_link_capacity(const char* str){
	char* tmp = strdup(str);
	const char prefix = pop_prefix(tmp);
	int multiplier = prefix_to_multiplier(prefix);

	if ( multiplier == -1 ){
		fprintf(stderr, "unknown prefix '%c' for --linkCapacity, ignored.\n", prefix);
		multiplier = 1;
	}

	set_link_capacity(atof(tmp) * multiplier);
	free(tmp);
}

void Extractor::set_extraction_level(const char* str){
	if (strcmp (optarg, "link") == 0)
		level = 0;
	else if ( strcmp (optarg, "network" ) == 0)
		level = 1;
	else if (strcmp (optarg ,"transport") == 0)
		level = 2;
	else if (strcmp (optarg , "application") == 0)
		level = 3;
	else {
		fprintf(stderr, "unrecognised level arg %s \n", optarg);
		exit(1);
	}
}

void Extractor::reset(){
	first_packet = true;
	bits = 0;
}

void Extractor::process_stream(const stream_t st, const struct filter* filter){
	const stream_stat_t* stat = stream_get_stat(st);
	int ret;

	while ( keep_running ) {
		/* A short timeout is used to allow the application to "breathe", i.e
		 * terminate if SIGINT was received. */
		struct timeval tv = {1,0};

		/* Read the next packet */
		cap_head* cp;
		ret = stream_read(st, &cp, filter, &tv);
		if ( ret == EAGAIN ){
			if ( !first_packet ){
				do_sample();
			}
			continue; /* timeout */
		} else if ( ret != 0 ){
			break; /* shutdown or error */
		}

		const int payLoadSize = payloadExtraction(level, cp); //payload size
		const qd_real current_time=(qd_real)(double)cp->ts.tv_sec+(qd_real)(double)(cp->ts.tv_psec/(double)PICODIVIDER); // extract timestamp.

		if ( first_packet ) {
			ref_time = current_time;
			start_time = ref_time;
			end_time = ref_time + tSample;
			remaining_samplinginterval = end_time - start_time;
			first_packet = false;
		}

		while ( keep_running && (to_double(current_time) - to_double(end_time)) >= 0.0){
			do_sample();
		}

		// estimate transfer time of the packet
		const qd_real transfertime_packet = (double)(payLoadSize*8) / link_capacity;
		qd_real remaining_transfertime = transfertime_packet;
		remaining_samplinginterval = end_time - current_time; //added now
		while ( keep_running && remaining_transfertime >= remaining_samplinginterval){
			bits += my_round(((to_double(remaining_samplinginterval))/(to_double(transfertime_packet)))*payLoadSize*8); //28 march
			remaining_transfertime-=remaining_samplinginterval;
			do_sample();
		}

		/* If the previous loop was broken by keep_running we should not sample the remaining data */
		if ( !keep_running ) break;

		// handle small packets or the remaining fractional packets which are in next interval
		bits+= my_round(((to_double(remaining_transfertime))/(to_double(transfertime_packet)))*payLoadSize*8);
		remaining_samplinginterval = end_time - current_time - transfertime_packet;

		if ( max_packets > 0 && stat->matched >= max_packets) {
			/* Read enough pkts lets break. */
			fprintf(stderr, "%s: read enought packages\n", program_name);
			break;
		}
	}

	/* if ret == -1 the stream was closed properly (e.g EOF or TCP shutdown)
	 * In addition EINTR should not give any errors because it is implied when the
	 * user presses C-c */
	if ( ret > 0 && ret != EINTR ){
		fprintf(stderr, "stream_read() returned 0x%08X: %s\n", ret, caputils_error_string(ret));
	}
}

void Extractor::do_sample(){
	const double t = to_double(start_time);
	const double bitrate = roundtwo(bits / to_double(tSample));

	write_sample(t, bitrate);

	// reset start_time ; end_time; remaining_sampling interval
	start_time = end_time;
	end_time = start_time + tSample;
	remaining_samplinginterval = tSample;
	bits = 0;
}


size_t Extractor::payloadExtraction(int level, const cap_head* caphead){
	// payload size at physical (ether+network+transport+app)
	if ( level == LEVEL_PHYSICAL ) {
		return caphead->len;
	};

	// payload size at link  (network+transport+app)
	if ( level == LEVEL_LINK ) {
		return caphead->len - sizeof(struct ethhdr);
	};

	const struct ethhdr *ether = caphead->ethhdr;
	const struct ip* ip_hdr = NULL;
	struct tcphdr* tcp = NULL;
	struct udphdr* udp = NULL;
	size_t vlan_offset = 0;

	switch(ntohs(ether->h_proto)) {
	case ETHERTYPE_IP:/* Packet contains an IP, PASS TWO! */
		ip_hdr = (struct ip*)(caphead->payload + sizeof(cap_header) + sizeof(struct ethhdr));
	ipv4:

		// payload size at network  (transport+app)
		if ( level == LEVEL_NETWORK ) {
			return ntohs(ip_hdr->ip_len)-4*ip_hdr->ip_hl;
		};

		switch(ip_hdr->ip_p) { /* Test what transport protocol is present */
		case IPPROTO_TCP: /* TCP */
			tcp = (struct tcphdr*)(caphead->payload + sizeof(cap_header) + sizeof(struct ethhdr) + vlan_offset + 4*ip_hdr->ip_hl);
			if( level == LEVEL_TRANSPORT ) return ntohs(ip_hdr->ip_len)-4*tcp->doff-4*ip_hdr->ip_hl;  // payload size at transport  (app)
			break;
		case IPPROTO_UDP: /* UDP */
			udp = (struct udphdr*)(caphead->payload + sizeof(cap_header) + sizeof(struct ethhdr) + vlan_offset + 4*ip_hdr->ip_hl);
			if( level == LEVEL_TRANSPORT ) return ntohs(udp->len)-8;                     // payload size at transport  (app)
			break;
		default:
			fprintf(stderr, "Unknown IP transport protocol: %d\n", ip_hdr->ip_p);
			return 0; /* there is no way to know the actual payload size here */
		}
		break;

	case ETHERTYPE_VLAN:
		ip_hdr = (struct ip*)(caphead->payload + sizeof(cap_header) + sizeof(struct ether_vlan_header));
		vlan_offset = 4;
		goto ipv4;

	case ETHERTYPE_IPV6:
		fprintf(stderr, "IPv6 not handled, ignored\n");
		return 0;

	case ETHERTYPE_ARP:
		fprintf(stderr, "ARP not handled, ignored\n");
		return 0;

	case STPBRIDGES:
		fprintf(stderr, "STP not handled, ignored\n");
		return 0;

	case CDPVTP:
		fprintf(stderr, "CDPVTP not handled, ignored\n");
		return 0;

	default:      /* Packet contains unknown link . */
		fprintf(stderr, "Unknown ETHERTYPE 0x%0x \n", ntohs(ether->h_proto));
		return 0; /* there is no way to know the actual payload size here, a zero will ignore it in the calculation */
	}

	fprintf(stderr, "packet wasn't handled by payLoadExtraction, ignored\n");
	return 0;
}