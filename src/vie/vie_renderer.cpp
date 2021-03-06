/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <re.h>
#include <avs.h>
#include <avs_vie.h>
#include "vie.h"
#include "vie_renderer.h"
#include "webrtc/common_video/libyuv/include/scaler.h"

extern "C" {
void frame_timeout_timer(void *arg)
{
	ViERenderer *renderer = (ViERenderer*)arg;

	if (renderer) {
		renderer->ReportTimeout();
	}
}
};

ViERenderer::ViERenderer()
	: _state(VIE_RENDERER_STATE_STOPPED)
{
	lock_alloc(&_lock);
	tmr_init(&_timer);
}

ViERenderer::~ViERenderer()
{
	tmr_cancel(&_timer);
	if (_state == VIE_RENDERER_STATE_RUNNING) {
		if (vid_eng.state_change_h) {
			vid_eng.state_change_h(FLOWMGR_VIDEO_RECEIVE_STOPPED,
				FLOWMGR_VIDEO_NORMAL, vid_eng.cb_arg);
		}
	}
	mem_deref(_lock);
}

void ViERenderer::OnFrame(const webrtc::VideoFrame& video_frame)
{
	struct avs_vidframe avs_frame;
	int err;
	
	lock_write_get(_lock);
	if (_state != VIE_RENDERER_STATE_RUNNING) {
		if (vid_eng.state_change_h) {
			vid_eng.state_change_h(FLOWMGR_VIDEO_RECEIVE_STARTED,
				FLOWMGR_VIDEO_NORMAL, vid_eng.cb_arg);
		}
		_state = VIE_RENDERER_STATE_RUNNING;
	}

	tmr_cancel(&_timer);
	tmr_start(&_timer, VIE_RENDERER_TIMEOUT_LIMIT, frame_timeout_timer, this);
	lock_rel(_lock);


	if (!vid_eng.render_frame_h)
		return;
	
	memset(&avs_frame, 0, sizeof(avs_frame));

	avs_frame.type = AVS_VIDFRAME_I420;
	avs_frame.y  = (uint8_t*)video_frame.video_frame_buffer()->DataY();
	avs_frame.u  = (uint8_t*)video_frame.video_frame_buffer()->DataU();
	avs_frame.v  = (uint8_t*)video_frame.video_frame_buffer()->DataV();
	avs_frame.ys = video_frame.video_frame_buffer()->StrideY();
	avs_frame.us = video_frame.video_frame_buffer()->StrideU();
	avs_frame.vs = video_frame.video_frame_buffer()->StrideV();
	avs_frame.w = video_frame.width();
	avs_frame.h = video_frame.height();
	avs_frame.ts = video_frame.timestamp();

	switch(video_frame.rotation()) {
	case webrtc::kVideoRotation_0:
		avs_frame.rotation = 0;
		break;

	case webrtc::kVideoRotation_90:
		avs_frame.rotation = 90;
		break;

	case webrtc::kVideoRotation_180:
		avs_frame.rotation = 180;
		break;

	case webrtc::kVideoRotation_270:
		avs_frame.rotation = 270;
		break;
	}

	err = vid_eng.render_frame_h(&avs_frame, vid_eng.cb_arg);
	if (err == ERANGE && vid_eng.size_h)
		vid_eng.size_h(avs_frame.w, avs_frame.h, vid_eng.cb_arg);
		
}

void ViERenderer::ReportTimeout()
{
	lock_read_get(_lock);

	if (_state == VIE_RENDERER_STATE_RUNNING)
	{
		if (vid_eng.state_change_h) {
			vid_eng.state_change_h(FLOWMGR_VIDEO_RECEIVE_STOPPED,
				FLOWMGR_VIDEO_BAD_CONNECTION, vid_eng.cb_arg);
		}
		_state = VIE_RENDERER_STATE_TIMEDOUT;
	}

	lock_rel(_lock);
}

