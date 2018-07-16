/*
 * main.cpp
 *
 *  Created on: Jul 16, 2018
 *      Author: HungNV
 */


#include <stdio.h>
#include "rtsprecorder.h"

int main(int argc, char** argv, char** envp) {
	av_register_all();
	avformat_network_init();

	unp::system::RTSPRecorder recorder;
	recorder.SetFileInfo(".", "unv01");
	recorder.OpenURL("rtsp://admin:123456@172.20.73.42", 15000);
	recorder.MainLoop();
	return 0;
}

