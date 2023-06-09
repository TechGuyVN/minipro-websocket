#ifndef __LWS_GLUE_H__
#define __LWS_GLUE_H__

#include "mod_audio_fork.h"


switch_status_t fork_init();
switch_status_t fork_cleanup();
switch_status_t fork_session_init(switch_core_session_t *session, responseHandler_t responseHandler,
		uint32_t samples_per_second, char *host, int sampling, int channels, char* metadata, void **ppUserData);
switch_status_t fork_session_cleanup(switch_core_session_t *session, char* text, int channelIsClosing);
switch_status_t fork_session_pauseresume(switch_core_session_t *session, int pause);
switch_status_t fork_session_graceful_shutdown(switch_core_session_t *session);
switch_status_t fork_session_send_text(switch_core_session_t *session, char* text);
switch_bool_t fork_frame(switch_core_session_t *session, switch_media_bug_t *bug);
switch_bool_t fork_frame_replace(switch_core_session_t *session, switch_media_bug_t *bug);
switch_status_t fork_service_threads();
#endif
