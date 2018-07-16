/*
 * rtsprecorder.cpp
 *
 *  Created on: Jul 16, 2018
 *      Author: HungNV
 */

#include "rtsprecorder.h"
#include <string.h>
#include <string>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <iostream>

namespace unp {
namespace system {

RTSPRecorder::RTSPRecorder()
: mInput(0)
, mOutput(0)
, mRunningFlag(true)
, mTimeLimits(LLONG_MAX)
, mPath(".")
, mFilePrefix("") {

}

RTSPRecorder::~RTSPRecorder() {
	mRunningFlag = false;
	CloseAll();
}

bool RTSPRecorder::OpenNewOutputFile() {
	if (!mInput) {
		return false;
	}

	if (!CloseOutputFile()) {
		return false;
	}

	mAudioOnly = true;
	std::ostringstream file_name;
	file_name.str("");
	file_name << mPath << '/' << mFilePrefix;
	char buffer[80];
	time_t rawtime;
	time(&rawtime);
	const auto timeinfo = localtime(&rawtime);
	strftime(buffer, sizeof(buffer), "%Y-%m-%d-%H-%M-%S", timeinfo);
	file_name << '-' << buffer << ".mkv";
	mStreamMap.clear();
	int stream_index = 0;

	int ret = avformat_alloc_output_context2(&mOutput, NULL, NULL, file_name.str().c_str());

	if (ret < 0) {
		avformat_close_input(&mInput);
		return false;
	}

	for(unsigned i = 0; i < mInput->nb_streams; i++) {
		AVStream* in_strm = mInput->streams[i];
		if (in_strm->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
				in_strm->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
				in_strm->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
			continue;
		}
		AVStream* strm = avformat_new_stream(mOutput, NULL);
		if (!strm) {
			avformat_free_context(mOutput);
			mOutput = NULL;
			avformat_close_input(&mInput);
			return false;
		}

		ret = avcodec_parameters_copy(strm->codecpar, mInput->streams[i]->codecpar);
		if (ret < 0) {
			fprintf(stderr, "Failed to copy codec parameters\n");
			avformat_free_context(mOutput);
			mOutput = NULL;
			avformat_close_input(&mInput);
			return false;
		}

		if (strm->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			mAudioOnly = false;

		strm->codecpar->codec_tag = 0;
		strm->time_base = mInput->streams[i]->time_base;
		strm->sample_aspect_ratio = mInput->streams[i]->sample_aspect_ratio;
		strm->avg_frame_rate = mInput->streams[i]->avg_frame_rate;
		strm->r_frame_rate = mInput->streams[i]->r_frame_rate;

		mStreamMap[i].idx = stream_index++;
		mStreamMap[i].begin_write = false;
	}

	ret = avio_open(&mOutput->pb, mOutput->url, AVIO_FLAG_WRITE);
	if (ret < 0) {
		avformat_free_context(mOutput);
		mOutput = NULL;
		avformat_close_input(&mInput);
		return false;
	}

	ret = avformat_write_header(mOutput, NULL);
	if (ret < 0) {
		avformat_free_context(mOutput);
		mOutput = NULL;
		avformat_close_input(&mInput);
		return false;
	}

	OnNewSegmentFile(mOutput->url);

	return true;
}

bool RTSPRecorder::CloseOutputFile() {
	if (!mOutput)
		return true;
	av_write_trailer(mOutput);
	avio_close(mOutput->pb);
	OnCloseSegmentFile(mOutput->url, mOutput->duration);
	avformat_free_context(mOutput);
	mOutput = NULL;

	return true;
}

bool RTSPRecorder::CloseAll() {
	CloseOutputFile();
	if (mInput) {
		avformat_close_input(&mInput);
	}

	return true;
}

bool RTSPRecorder::OpenURL(const char* url, int64_t time_limit) {
	if(mInput) {
		avformat_close_input(&mInput);
	}

	AVDictionary *options = NULL;
	av_dict_set(&options, "stimeout", "5000000", 0); // 5 seconds in microseconds
	int ret = avformat_open_input(&mInput, url, NULL, &options);
	av_dict_free(&options);

	if (ret < 0) {
//		LOG_ERROR("Cam " << m_Camera->m_Id.c_str() << " avformat_open_input fail");
		return false;
	}

	ret = avformat_find_stream_info(mInput, NULL);
	if(ret < 0) {
		avformat_close_input(&mInput);
//		LOG_ERROR("Cam " << m_Camera->m_Id.c_str() << " avformat_find_stream_info fail");
		return false;
	}


	for(unsigned i = 0; i < mInput->nb_streams; i++) {
		OnInitializeStream(mInput->streams[i]);
	}

	if (!OpenNewOutputFile()) {
		avformat_close_input(&mInput);
		return false;
	}

	mTimeLimits = time_limit;
	return true;
}

void RTSPRecorder::SetFileInfo(const char* path, const char* file_prefix) {
	mPath = path;
	mFilePrefix = file_prefix;
}

void RTSPRecorder::SetTerminate() {
	mRunningFlag = false;
}

void RTSPRecorder::MainLoop() {
	AVPacket pkt;
	AVStream *in_stream, *out_stream;
	int ret = -1;
	bool begin_write = false;
	int64_t pts, dts;
	while (mRunningFlag) {
		ret = av_read_frame(mInput, &pkt);

		if (ret != 0) {
			if (ret == AVERROR(EAGAIN)) {//read again (cam don't return packet)
				av_usleep(10000);
				std::cout << "Read again" << std::endl;
				continue;
			} else {
				mRunningFlag = false;
				break;
			}
		}
		else { // got package
			if (mStreamMap.find(pkt.stream_index) == mStreamMap.end()) {// no map stream
				av_packet_unref(&pkt);
				continue;
			}
			in_stream  = mInput->streams[pkt.stream_index];
			out_stream = mOutput->streams[mStreamMap[pkt.stream_index].idx];

			if (pkt.pts != AV_NOPTS_VALUE)
				pts = av_rescale_q(pkt.pts, in_stream->time_base, out_stream->time_base);
			else
				pts = av_rescale_q(0, AV_TIME_BASE_Q, out_stream->time_base);

			if (pkt.dts != AV_NOPTS_VALUE)
				dts = av_rescale_q(pkt.dts, in_stream->time_base, out_stream->time_base);
			else
				dts = av_rescale_q(0, AV_TIME_BASE_Q, out_stream->time_base);

			if (!mStreamMap[pkt.stream_index].begin_write) {
				mStreamMap[pkt.stream_index].pts = pts;
				mStreamMap[pkt.stream_index].dts = dts;
			}

			if(!begin_write) {
				if (mAudioOnly)
					begin_write = true;
				else if((pkt.flags & AV_PKT_FLAG_KEY) && (out_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
					begin_write = true;
				else {
					av_packet_unref(&pkt);
					continue;
				}
			}

			mStreamMap[pkt.stream_index].begin_write = true;
			OnPackage(pkt.stream_index, pkt.data, pkt.size, pkt.pts, pkt.dts, pkt.duration);

			if (mAudioOnly) {
				if (dts - mStreamMap[pkt.stream_index].dts >= mTimeLimits) { // over time limit, open new segment file
					OpenNewOutputFile();
					for (auto it = mStreamMap.begin(); it != mStreamMap.end(); ++it) {
						it->second.begin_write = false;
					}
					mStreamMap[pkt.stream_index].begin_write = true;
					mStreamMap[pkt.stream_index].pts = pts;
					mStreamMap[pkt.stream_index].dts = dts;
				}
			} else {
				if ((dts - mStreamMap[pkt.stream_index].dts >= mTimeLimits) &&
						(out_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
						(pkt.flags & AV_PKT_FLAG_KEY)) { //reset now
					OpenNewOutputFile();
					for (auto it = mStreamMap.begin(); it != mStreamMap.end(); ++it) {
						it->second.begin_write = false;
					}
					mStreamMap[pkt.stream_index].begin_write = true;
					mStreamMap[pkt.stream_index].pts = pts;
					mStreamMap[pkt.stream_index].dts = dts;
				}
			}

			if (pkt.pts != AV_NOPTS_VALUE)
				pkt.pts = pts - mStreamMap[pkt.stream_index].pts;
			else
				pkt.pts = pts;

			if (pkt.dts != AV_NOPTS_VALUE)
				pkt.dts = dts - mStreamMap[pkt.stream_index].dts;
			else
				pkt.dts = dts;

			pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
			pkt.pos = -1;
			pkt.stream_index = mStreamMap[pkt.stream_index].idx;

			ret = av_interleaved_write_frame(mOutput, &pkt);

			if(ret <0) {
				std::cout << "write frame error" << std::endl;
				//				LOG_ERROR("Write frame error:" << l_Ret);
			}

			av_packet_unref(&pkt);
		}
	}
}

void RTSPRecorder::OnInitializeStream(const AVStream* strm_info) {
}

void RTSPRecorder::OnPackage(int stream_idx, const uint8_t* data, int data_size,
		int64_t pts, int64_t dts, int64_t duration) {
}

void RTSPRecorder::OnNewSegmentFile(const char* file_path) {
	std::cout << "Openned new file with name = \"" << file_path << "\"" << std::endl;
}

void RTSPRecorder::OnCloseSegmentFile(const char* file_path, int64_t file_long) {
	std::cout << "Closed file with name = \"" << file_path << "\" and duration = " << file_long << std::endl;
}

} /* namespace system */
} /* namespace unp */
