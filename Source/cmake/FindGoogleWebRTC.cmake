# - Try to find OpenWebRTC.
# Once done, this will define
#
#  OPENWEBRTC_FOUND - system has OpenWebRTC.
#  OPENWEBRTC_INCLUDE_DIRS - the OpenWebRTC include directories
#  OPENWEBRTC_LIBRARIES - link these to use OpenWebRTC.
#
# Copyright (C) 2015 Igalia S.L.
# Copyright (C) 2015 Metrological.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND ITS CONTRIBUTORS ``AS
# IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR ITS
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


find_package(PkgConfig)
set(VERSION_OK TRUE)

set(GOOGLEWEBRTC_INCLUDE_DIR "/home/temasys/webkit-openwebrtc-libwebrtc/WebKitBuild/DependenciesGTK/Root/include/googlewebrtc")
set(GOOGLEWEBRTC_INCLUDE_DIRS "${GOOGLEWEBRTC_INCLUDE_DIR}/third_party/libvpx/source/libvpx" "${GOOGLEWEBRTC_INCLUDE_DIR}/webrtc" "${GOOGLEWEBRTC_INCLUDE_DIR}/third_party" "${GOOGLEWEBRTC_INCLUDE_DIR}" "/usr/include/nss" "/usr/include/nspr")

set(GOOGLEWEBRTC_LIBRARY_PATHS "/home/temasys/webkit-openwebrtc-libwebrtc/WebKitBuild/DependenciesGTK/Root/lib64/googlewebrtc")

#ADDING ALL LIBRARIES IN THE VARIABLE GOOGLE_PEERCONNECTION_LIBRARIES
set(GOOGLEWEBRTC_PEERCONNECTION_LIBRARIES "${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/talk/libjingle_peerconnection.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/jsoncpp/libjsoncpp.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/talk/libjingle_media.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libvideo_render_module.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libwebrtc_utility.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libaudio_coding_module.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libCNG.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/openmax_dl/dl/libopenmax_dl.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/common_audio/libcommon_audio_sse2.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libaudio_encoder_interface.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libG722.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libG711.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libiLBC.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libiSAC.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libaudio_decoder_interface.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libiSACFix.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libPCM16B.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libred.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libwebrtc_opus.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/opus/libopus.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libneteq.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libmedia_file.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/common_video/libcommon_video.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/libyuv.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/libjpeg_turbo/libjpeg_turbo.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/libwebrtc.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/video_engine/libvideo_engine_core.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/librtp_rtcp.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libpaced_sender.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libremote_bitrate_estimator.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libbitrate_controller.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libwebrtc_video_coding.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libwebrtc_i420.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/video_coding/utility/libvideo_coding_utility.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/video_coding/codecs/vp8/libwebrtc_vp8.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/libvpx/libvpx_intrinsics_mmx.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/libvpx/libvpx_intrinsics_sse2.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/libvpx/libvpx_intrinsics_ssse3.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/libvpx/libvpx_intrinsics_sse4_1.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/libvpx/libvpx_intrinsics_avx2.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/video_coding/codecs/vp9/libwebrtc_vp9.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/libvpx/libvpx.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libvideo_processing.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libvideo_processing_sse2.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/voice_engine/libvoice_engine.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libaudio_conference_mixer.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libaudio_processing.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libvideo_capture_module_internal_impl.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libvideo_capture_module.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libaudioproc_debug_proto.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/protobuf/libprotobuf_lite.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libaudio_processing_sse2.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libaudio_device.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/sound/librtc_sound.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/system_wrappers/libfield_trial_default.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/system_wrappers/libmetrics_default.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/libjingle/xmllite/librtc_xmllite.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/libjingle/xmpp/librtc_xmpp.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/p2p/librtc_p2p.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/usrsctp/libusrsctplib.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/modules/libvideo_render_module_internal_impl.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/talk/libjingle_p2p.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/libsrtp/libsrtp.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/third_party/boringssl/libboringssl.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/common_audio/libcommon_audio.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/base/librtc_base.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/system_wrappers/libsystem_wrappers.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/base/librtc_base_approved.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/webrtc/libwebrtc_common.a;${GOOGLEWEBRTC_LIBRARY_PATHS}/obj/chromium/src/net/third_party/nss/libcrssl.a;m;expat;nspr4;plc4;plds4;smime3;nss3;nssutil3;Xrender;Xcomposite;X11;Xext;rt;dl;freetype;glib-2.0;gobject-2.0;fontconfig;pango-1.0;cairo;gdk_pixbuf-2.0;pangocairo-1.0;pangoft2-1.0;pangoft2-1.0;gio-2.0;atk-1.0;gdk-x11-2.0;gtk-x11-2.0;gthread-2.0")

set(GOOGLEWEBRTC_LIBRARIES ${GOOGLEWEBRTC_PEERCONNECTION_LIBRARIES})

# handle the QUIETLY and REQUIRED arguments and set DirectX_FOUND to TRUE if all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GOOGLEWEBRTC DEFAULT_MSG GOOGLEWEBRTC_INCLUDE_DIRS GOOGLEWEBRTC_LIBRARIES VERSION_OK)




