/*
 * rtsprecorder.h
 *
 *  Created on: Jul 16, 2018
 *      Author: HungNV
 */

#ifndef RTSPRECORDER_H_
#define RTSPRECORDER_H_

#include <stdint.h>
#include <climits>
#include <string>
#include <map>

#ifdef __cplusplus
extern "C"
{
#endif
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/mathematics.h>
#ifdef __cplusplus
}
#endif

namespace unp {
namespace system {

class RTSPRecorder {
protected:
	typedef struct StreamTimeInfo {
		int idx;
		int64_t dts;
		int64_t pts;
		int64_t last_dts;
		int64_t last_pts;
		bool begin_write;
	}StreamTimeInfo;

	AVFormatContext* 	mInput;
	AVFormatContext*	mOutput;
	bool mRunningFlag;
	bool mAudioOnly;
	int64_t mTimeLimits;
	std::string mPath;
	std::string mFilePrefix;
	std::map<int, StreamTimeInfo> mStreamMap;

	bool OpenNewOutputFile();
	bool CloseOutputFile();
	bool CloseAll();
public:
	RTSPRecorder();
	virtual ~RTSPRecorder();

	virtual bool OpenURL(const char* url, int64_t time_limit = LLONG_MAX);
	void SetFileInfo(const char* path, const char* file_prefix);
	void SetTerminate();
	virtual void MainLoop();
	virtual void OnInitializeStream(const AVCodecParameters* strm_info);
	virtual void OnPackage(int stream_idx, const uint8_t* data, int data_size, int64_t pts, int64_t dts, int64_t duration);
	virtual void OnNewSegmentFile(const char* file_path);
	virtual void OnCloseSegmentFile(const char* file_path, int64_t file_long);
};

} /* namespace system */
} /* namespace unp */

#endif /* RTSPRECORDER_H_ */
