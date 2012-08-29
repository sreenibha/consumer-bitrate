#ifndef EXTRACT_H
#define EXTRACT_H

#include <caputils/caputils.h>
#include <qd/qd_real.h>

enum Level {
	LEVEL_INVALID     = -1,
	LEVEL_PHYSICAL    = 0,
	LEVEL_LINK        = 1,
	LEVEL_NETWORK     = 2,
	LEVEL_TRANSPORT   = 3,
	LEVEL_APPLICATION = 4,
};

/**
 * Controls whenever the application should run or not.
 */
extern bool keep_running;

class Extractor {
public:
	Extractor();
	virtual ~Extractor();

	/**
	 * Force counters and accumulators to reset.
	 */
	virtual void reset();

	/**
	 * Process packets in stream.
	 */
	void process_stream(const stream_t st, const struct filter* filter);

	/**
	 * Stop processing packets.
	 * This has the same effect as setting the global keep_running to false.
	 */
	void stop();

	/**
	 * Set sampling frequency in Hz.
	 */
	void set_sampling_frequency(double hz);

	/**
	 * Set sampling frequency from string.
	 * Supports prefixes: 'k', 'm', and 'g'.
	 */
	void set_sampling_frequency(const char* str);

	/**
	 * Set level to extract size from.
	 */
	void set_extraction_level(const char* str);

	/**
	 * Stop processing after N packets.
	 */
	void set_max_packets(size_t n);

	/**
	 * Set link capacity in bits per second.
	 */
	void set_link_capacity(unsigned long bps);

	/**
	 * Set link capacity from string.
	 * Supports prefixes: 'k', 'm', 'g'.
	 */
	void set_link_capacity(const char* str);

protected:
	/**
	 * Write a sample.
	 */
	virtual void write_sample(double t) = 0;

	/**
	 * Accumulate value from packet.
	 *
	 * @param bits Total number of bits in packet.
	 * @param cp Packet header.
	 * @param counter Number of times this packet has been sampled.
	 */
	virtual void accumulate(qd_real fraction, unsigned long bits, const cap_head* cp, int counter) = 0;

	/**
	 * Calculate bitrate for current sample and move time forward.
	 */
	void do_sample();

	/**
	 * Extracts the number of bytes at specified level.
	 * @return size in bytes.
	 */
	size_t payloadExtraction(int level, const cap_head* caphead);

	qd_real ref_time;
	qd_real start_time;
	qd_real end_time;
	qd_real remaining_samplinginterval;
	qd_real remaining_transfertime;
	qd_real transfertime_packet;
	double sampleFrequency;
	qd_real tSample;

private:
	bool first_packet;
	unsigned int max_packets;
	unsigned long link_capacity;
	int level;
};

#endif /* EXTRACT_H */
