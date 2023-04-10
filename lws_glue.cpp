/** 
 * Modify by viethq@vihat.vn
 * 
 * important function:
 * processIncomingMessage
 * fork_data_init: init data of single session forked
 * fork_init : call firsttime mod start
 * fork_frame_replace -> read audio stream and stream to socket kaldi AI
 * 
*/
#include <switch.h>
#include <switch_stun.h>
#include <switch_json.h>
#include <string.h>
#include <string>
#include <mutex>
#include <thread>
#include <list>
#include <algorithm>
#include <functional>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <regex>
#include <unistd.h>

#include "base64.hpp"
#include "parser.hpp"
#include "mod_audio_fork.h"
#include "audio_pipe.hpp"


#define RTP_PACKETIZATION_PERIOD 20
#define FRAME_SIZE_8000 320 /*which means each 20ms frame as 320 bytes at 8 khz (1 channel only)*/

namespace
{
  static const char *requestedBufferSecs = std::getenv("MOD_AUDIO_FORK_BUFFER_SECS");
  static int nAudioBufferSecs = std::max(1, std::min(requestedBufferSecs ? ::atoi(requestedBufferSecs) : 2, 100));
  static const char *requestedNumServiceThreads = std::getenv("MOD_AUDIO_FORK_SERVICE_THREADS");
  static const char *mySubProtocolName = std::getenv("MOD_AUDIO_FORK_SUBPROTOCOL_NAME") ? std::getenv("MOD_AUDIO_FORK_SUBPROTOCOL_NAME") : "audio.drachtio.org";
  static unsigned int nServiceThreads = std::max(1, std::min(requestedNumServiceThreads ? ::atoi(requestedNumServiceThreads) : 1, 100));
  static unsigned int idxCallCount = 0;
  static uint32_t playCount = 0;

  void processIncomingMessage(private_t *tech_pvt, switch_core_session_t *session, const char *message)
  {
    std::string msg = message;
    std::string type;
    cJSON *json = parse_json(session, msg, type);
    double sleepStr = 0.0;
    const char *uuid = switch_core_session_get_uuid(session);

    if(message  == nullptr){
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "processIncomingMessage  message is null\n");
      return;
    }else{
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "processIncomingMessage %s\n", msg.c_str());
    }

    if (json)
    {
      switch_channel_t *channel = switch_core_session_get_channel(session);
      // int final_value = cJSON_GetObjectItem(json, "final");
      cJSON *jsonData = cJSON_GetObjectItem(json, "data");
      if (0 == type.compare("playAudio"))
      {
        cJSON *final_value = cJSON_GetObjectItem(json, "final");
        cJSON *sip_number_str = cJSON_GetObjectItem(json, "sip_number");
        if (jsonData)
        {
          // dont send actual audio bytes in event message
          cJSON *audioDuration = cJSON_GetObjectItem(jsonData, "audioDuration");
          if (audioDuration)
          {
            sleepStr = audioDuration->valuedouble + 1;
          }
          cJSON *jsonFile = NULL;
          cJSON *jsonAudio = cJSON_DetachItemFromObject(jsonData, "audioContent");
          int validAudio = (jsonAudio && NULL != jsonAudio->valuestring);

          if(audioDuration && audioDuration->valuedouble <= 1){
            validAudio = 0;
          }

          const char *szAudioContentType = cJSON_GetObjectCstr(jsonData, "audioContentType");
          char fileType[6];
          int sampleRate = 8000;
          if (0 == strcmp(szAudioContentType, "raw"))
          {
            cJSON *jsonSR = cJSON_GetObjectItem(jsonData, "sampleRate");
            sampleRate = jsonSR && jsonSR->valueint ? jsonSR->valueint : 0;

            switch (sampleRate)
            {
            case 8000:
              strcpy(fileType, ".r8");
              break;
            case 16000:
              strcpy(fileType, ".r16");
              break;
            case 22050:
              strcpy(fileType, ".r16");
              break;
            case 24000:
              strcpy(fileType, ".r24");
              break;
            case 32000:
              strcpy(fileType, ".r32");
              break;
            case 48000:
              strcpy(fileType, ".r48");
              break;
            case 64000:
              strcpy(fileType, ".r64");
              break;
            default:
              strcpy(fileType, ".r16");
              break;
            }
          }
          else if (0 == strcmp(szAudioContentType, "wave") || 0 == strcmp(szAudioContentType, "wav"))
          {
            strcpy(fileType, ".wav");
          }
          else
          {
            validAudio = 0;
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) processIncomingMessage - unsupported audioContentType: %s\n", tech_pvt->id, szAudioContentType);
          }

          /**
           * @brief If audio valid then we will play it to client
           * 
           */
          if (validAudio)
          {
            char szFilePath[256];

            std::string rawAudio = drachtio::base64_decode(jsonAudio->valuestring);
            switch_snprintf(szFilePath, 256, "%s%s%s_%d.tmp%s", SWITCH_GLOBAL_dirs.temp_dir,
                            SWITCH_PATH_SEPARATOR, tech_pvt->sessionId, playCount++, fileType);
            std::ofstream f(szFilePath, std::ofstream::binary);
            f << rawAudio;
            f.close();

            // add the file to the list of files played for this session, we'll delete when session closes
            struct playout *playout = (struct playout *)malloc(sizeof(struct playout));
            playout->file = (char *)malloc(strlen(szFilePath) + 1);
            strcpy(playout->file, szFilePath);
            playout->next = tech_pvt->playout;
            tech_pvt->playout = playout;

            jsonFile = cJSON_CreateString(szFilePath);
            cJSON_AddItemToObject(jsonData, "file", jsonFile);

            switch_media_flag_t flags = SMF_NONE;
            // flags |= (SMF_ECHO_ALEG | SMF_ECHO_BLEG);
            const char* isSleepStartCallbot = switch_channel_get_variable(channel, "is_sleep_start_callbot");
            if(!isSleepStartCallbot || (isSleepStartCallbot && strcmp(isSleepStartCallbot, "true") != 0)) {
              switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "(%u) processIncomingMessage - first time delay 1.5 second\n", tech_pvt->id);
              switch_channel_set_variable(channel, "is_sleep_start_callbot", "true");
              usleep(1500000);
            }
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 1 \n");

            switch_ivr_broadcast(uuid, szFilePath, SMF_ECHO_ALEG);
          }

          char *jsonString = cJSON_PrintUnformatted(jsonData);
          tech_pvt->responseHandler(session, EVENT_PLAY_AUDIO, jsonString);
          free(jsonString);
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 2 \n");

          if (jsonAudio)
            cJSON_Delete(jsonAudio);
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 3 \n");

        }
        else
        {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "(%u) processIncomingMessage - missing data payload in playAudio request\n", tech_pvt->id);
        }
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 4 \n");

        if (cJSON_IsTrue(final_value) == 1)
        {
          tech_pvt->isClosing = 1;
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 5 %f \n",sleepStr);
          // usleep(int(sleepStr * 1000000));
          // switch_status_t st;
          // switch_channel_t *channel = switch_core_session_get_channel(session);
          // st = switch_channel_wait_for_flag(channel, CF_BROADCAST, SWITCH_TRUE, 0, NULL);
          // const char *uuid = switch_core_session_get_uuid(session);
          // switch_ivr_kill_uuid(uuid,SWITCH_CAUSE_NORMAL_CLEARING);
          if (sip_number_str && sip_number_str->valuestring != NULL && strcmp(sip_number_str->valuestring, "") != 0)
          {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "sip_number received: %s\n", sip_number_str->valuestring);
            switch_channel_set_variable(channel, "sip_number_ai", sip_number_str->valuestring);
            switch_channel_set_flag_value(channel, CF_BREAK, 2);
            switch_channel_set_flag_value(channel, CF_BREAK, 1);
          }
          else
          {
              // switch_ivr_kill_uuid(uuid, SWITCH_CAUSE_NORMAL_CLEARING);
            switch_stream_handle_t stream = { 0 };
            SWITCH_STANDARD_STREAM(stream);
            const char *cmnd = "+";
            std::string str1 = std::to_string(int(sleepStr));
            const char *cmnd2 = str1.c_str();
            const char *cmnd3 = " ";

            char *command = new char[strlen(cmnd)+strlen(cmnd2)+strlen(cmnd3)+strlen(uuid)+1];
            strcpy(command,cmnd);
            strcat(command,cmnd2);
            strcat(command,cmnd3);
            strcat(command,uuid);

            // std::string str1 = "+";
            // std::string str2 = std::to_string(int(sleepStr));
            // std::string str3 = " " + std::string(uuid);
            // const char *command = (str1 + str2 + str3).c_str();
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 6.1 %s \n", command);

            switch_api_execute("sched_hangup", command, NULL, &stream);
            // delete [] s;
          }
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 7 \n");
          // return;
        }
      }
      else if (0 == type.compare("aiResultVoiceVerify") && jsonData)
      {
                  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 8 \n");

        cJSON *result = cJSON_GetObjectItem(jsonData, "result");
        if (result)
        {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) processIncomingMessage full - received %s message and full_msg: %s\n", tech_pvt->id, type.c_str(), msg.c_str());
          switch_core_session_t *other_session;
          switch_channel_t *other_channel;
          if (switch_core_session_get_partner(session, &other_session) == SWITCH_STATUS_SUCCESS)
          {

            other_channel = switch_core_session_get_channel(other_session);
            if (result->valueint == 1)
            {
              switch_channel_set_variable(other_channel, "ai_result_voice_verify", "true");
              switch_channel_set_variable(channel, "ai_result_voice_verify", "true");
            }
            else
            {
              switch_channel_set_variable(other_channel, "ai_result_voice_verify", "false");
              switch_channel_set_variable(channel, "ai_result_voice_verify", "false");
            }
          }
        }
      }
      else if (0 == type.compare("killAudio"))
      {
                  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 9 \n");

        tech_pvt->responseHandler(session, EVENT_KILL_AUDIO, NULL);

        // kill any current playback on the channel
        switch_channel_set_flag_value(channel, CF_BREAK, 2);
      }
      else if (0 == type.compare("transcription"))
      {
        char *jsonString = cJSON_PrintUnformatted(jsonData);
        tech_pvt->responseHandler(session, EVENT_TRANSCRIPTION, jsonString);
        free(jsonString);
      }
      else if (0 == type.compare("transfer"))
      {
        char *jsonString = cJSON_PrintUnformatted(jsonData);
        tech_pvt->responseHandler(session, EVENT_TRANSFER, jsonString);
        free(jsonString);
      }
      else if (0 == type.compare("disconnect"))
      {
        char *jsonString = cJSON_PrintUnformatted(jsonData);
        tech_pvt->responseHandler(session, EVENT_DISCONNECT, jsonString);
        free(jsonString);
      }
      else if (0 == type.compare("error"))
      {
        char *jsonString = cJSON_PrintUnformatted(jsonData);
        tech_pvt->responseHandler(session, EVENT_ERROR, jsonString);
        free(jsonString);
      }
      else if (0 == type.compare("json"))
      {
        char *jsonString = cJSON_PrintUnformatted(json);
        tech_pvt->responseHandler(session, EVENT_JSON, jsonString);
        free(jsonString);
      }
      else
      {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "(%u) processIncomingMessage - unsupported msg type %s\n", tech_pvt->id, type.c_str());
      }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 10 \n");

      cJSON_Delete(json);
                      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Step 11 \n");

      // free(channel);
    }
    else
    {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) processIncomingMessage - could not parse message: %s\n", tech_pvt->id, message);
    }
  }

  static void eventCallback(const char *sessionId, AudioPipe::NotifyEvent_t event, const char *message)
  {
    switch_core_session_t *session = switch_core_session_locate(sessionId);
    if (session)
    {
      switch_channel_t *channel = switch_core_session_get_channel(session);
      switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, MY_BUG_NAME);
      if (bug)
      {
        private_t *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);
        if (tech_pvt)
        {
          switch (event)
          {
          case AudioPipe::CONNECT_SUCCESS:
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "connection successful\n");
            tech_pvt->responseHandler(session, EVENT_CONNECT_SUCCESS, NULL);
            
            break;
          case AudioPipe::CONNECT_FAIL:
          {
            // first thing: we can no longer access the AudioPipe
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "connection failed: %s\n", message);
            std::stringstream json;
            json << "{\"reason\":\"" << message << "\"}";
            tech_pvt->pAudioPipe = nullptr;
            tech_pvt->responseHandler(session, EVENT_CONNECT_FAIL, (char *)json.str().c_str());
            // switch_channel_set_flag_value(channel, CF_BREAK, 2);
            // switch_channel_set_flag_value(channel, CF_BREAK, 1);
            switch_ivr_kill_uuid(tech_pvt->sessionId, SWITCH_CAUSE_GATEWAY_DOWN);
          }
          break;
          case AudioPipe::CONNECTION_DROPPED:
            // first thing: we can no longer access the AudioPipe
            tech_pvt->pAudioPipe = nullptr;
            tech_pvt->responseHandler(session, EVENT_DISCONNECT, NULL);
            // switch_channel_set_flag_value(channel, CF_BREAK, 2);
            // switch_channel_set_flag_value(channel, CF_BREAK, 1);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "connection dropped from far end Viet\n");
            // switch_ivr_kill_uuid(tech_pvt->sessionId, SWITCH_CAUSE_NORMAL_CLEARING);

            break;
          case AudioPipe::CONNECTION_CLOSED_GRACEFULLY:
            // first thing: we can no longer access the AudioPipe
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection closed gracefully\n");
            tech_pvt->pAudioPipe = nullptr;
            // switch_channel_set_flag_value(channel, CF_BREAK, 2);
            // switch_channel_set_flag_value(channel, CF_BREAK, 1);
            // switch_ivr_kill_uuid(tech_pvt->sessionId, SWITCH_CAUSE_NORMAL_CLEARING);
            break;
          case AudioPipe::MESSAGE:
            processIncomingMessage(tech_pvt, session, message);
            break;
          }
        }
      }
      switch_core_session_rwunlock(session);
    }
  }
  switch_status_t fork_data_init(private_t *tech_pvt, switch_core_session_t *session, char *host, int sampling, int desiredSampling, int channels, char *metadata, responseHandler_t responseHandler)
  {

    const char *username = nullptr;
    const char *password = nullptr;
    int err;
    switch_codec_implementation_t read_impl;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    switch_core_session_get_read_impl(session, &read_impl);

    if (username = switch_channel_get_variable(channel, "MOD_AUDIO_BASIC_AUTH_USERNAME"))
    {
      password = switch_channel_get_variable(channel, "MOD_AUDIO_BASIC_AUTH_PASSWORD");
    }

    memset(tech_pvt, 0, sizeof(private_t));

    strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
    tech_pvt->sampling = desiredSampling;
    tech_pvt->responseHandler = responseHandler;
    tech_pvt->playout = NULL;
    tech_pvt->channels = channels;
    tech_pvt->id = ++idxCallCount;
    tech_pvt->audio_paused = 0;
    tech_pvt->graceful_shutdown = 0;
    /*
    - channel alway 1 in omicall AI callbot
    - nAudioBufferSecs = 2
    : 320 * 8000/8000 * 1 * 1000/20*2  */
    AudioPipe *ap = new AudioPipe(tech_pvt->sessionId, host, username, password, eventCallback);
    if (!ap)
    {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error allocating AudioPipe\n");
      return SWITCH_STATUS_FALSE;
    }

    tech_pvt->pAudioPipe = static_cast<void *>(ap);

    switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));

    if (desiredSampling != sampling)
    {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) resampling from %u to %u\n", tech_pvt->id, sampling, desiredSampling);
      tech_pvt->resampler = speex_resampler_init(channels, sampling, desiredSampling, SWITCH_RESAMPLE_QUALITY, &err);
      if (0 != err)
      {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error initializing resampler: %s.\n", speex_resampler_strerror(err));
        return SWITCH_STATUS_FALSE;
      }
    }
    else
    {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) no resampling needed for this call\n", tech_pvt->id);
    }

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_data_init\n", tech_pvt->id);

    return SWITCH_STATUS_SUCCESS;
  }

  void destroy_tech_pvt(private_t *tech_pvt)
  {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s (%u) destroy_tech_pvt\n", tech_pvt->sessionId, tech_pvt->id);
    if (tech_pvt->resampler)
    {
      speex_resampler_destroy(tech_pvt->resampler);
      tech_pvt->resampler = nullptr;
    }
    if (tech_pvt->mutex)
    {
      switch_mutex_destroy(tech_pvt->mutex);
      tech_pvt->mutex = nullptr;
    }
  }

  void omi_logger( const char *line)
  {
    switch_log_level_t llevel = SWITCH_LOG_DEBUG;
    switch_log_printf(SWITCH_CHANNEL_LOG, llevel, "%s\n", line);

    // switch (level)
    // {
    // case LLL_ERR:
    //   llevel = SWITCH_LOG_ERROR;
    //   break;
    // case LLL_WARN:
    //   llevel = SWITCH_LOG_WARNING;
    //   break;
    // case LLL_NOTICE:
    //   llevel = SWITCH_LOG_NOTICE;
    //   break;
    // case LLL_INFO:
    //   llevel = SWITCH_LOG_INFO;
    //   break;
    //   break;
    // }
  }
}

extern "C"
{
 

  switch_status_t fork_init()
  {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: audio buffer (in secs):    %d secs\n", nAudioBufferSecs);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: sub-protocol:              %s\n", mySubProtocolName);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: lws service threads:       %d\n", nServiceThreads);
    // LLL_INFO | LLL_PARSER | LLL_HEADER | LLL_EXT | LLL_CLIENT  | LLL_LATENCY | LLL_DEBUG ;
    AudioPipe::initialize(omi_logger);
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_cleanup()
  {
    bool cleanup = false;
    cleanup = AudioPipe::deinitialize();
    if (cleanup == true)
    {
      return SWITCH_STATUS_SUCCESS;
    }
    return SWITCH_STATUS_FALSE;
  }


  /*
  * init data adapt to session 
  *
  */

  switch_status_t fork_session_init(switch_core_session_t *session,
                                    responseHandler_t responseHandler,
                                    uint32_t samples_per_second,
                                    char *host,
                                    int sampling,
                                    int channels,
                                    char *metadata,
                                    void **ppUserData)
  {
    int err;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "begin fork_session_init!\n");

    // allocate per-session data structure
    private_t *tech_pvt = (private_t *)switch_core_session_alloc(session, sizeof(private_t));
    if (!tech_pvt)
    {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "error allocating memory!\n");
      return SWITCH_STATUS_FALSE;
    }
    if (SWITCH_STATUS_SUCCESS != fork_data_init(tech_pvt, session, host, samples_per_second, sampling, channels, metadata, responseHandler))
    {
      destroy_tech_pvt(tech_pvt);
      return SWITCH_STATUS_FALSE;
    }

    *ppUserData = tech_pvt;

    AudioPipe *pAudioPipe = static_cast<AudioPipe *>(tech_pvt->pAudioPipe);
    pAudioPipe->connect();
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_cleanup(switch_core_session_t *session, char *text, int channelIsClosing)
  {
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "begin fork_session_cleanup!\n");

    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug)
    {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "fork_session_cleanup: no bug - websocket conection already closed\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);
    uint32_t id = tech_pvt->id;

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup\n", id);

    if (!tech_pvt || (tech_pvt && !tech_pvt->mutex)){
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup error techvt null\n", id);
        return SWITCH_STATUS_FALSE;
    }
      

    AudioPipe *pAudioPipe =  nullptr;
    if(tech_pvt->pAudioPipe){
      pAudioPipe = static_cast<AudioPipe *>(tech_pvt->pAudioPipe);
    } 

    switch_mutex_trylock(tech_pvt->mutex);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup 1 \n", id);

    // get the bug again, now that we are under lock
    {
      switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, MY_BUG_NAME);
      if (bug)
      {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup 2 \n", id);
        switch_channel_set_private(channel, MY_BUG_NAME, NULL);
        if (!channelIsClosing)
        {
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup 3 \n", id);
          switch_core_media_bug_remove(session, &bug);
          
        }
      }
    }
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup 4 \n", id);

    // delete any temp files
    struct playout *playout = tech_pvt->playout;
    while (playout)
    {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup 5 \n", id);
      std::remove(playout->file);
      free(playout->file);
      struct playout *tmp = playout;
      playout = playout->next;
      free(tmp);
    }
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup 5.1 \n", id);

    if (pAudioPipe)
      pAudioPipe->close();
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup 6 \n", id);

    destroy_tech_pvt(tech_pvt);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "(%u) fork_session_cleanup: connection closed\n", id);
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_send_text(switch_core_session_t *session, char *text)
  {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "begin fork_session_send_text!\n");
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug)
    {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "fork_session_send_text failed because no bug\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);

    if (!tech_pvt)
      return SWITCH_STATUS_FALSE;
    AudioPipe *pAudioPipe = static_cast<AudioPipe *>(tech_pvt->pAudioPipe);
    
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_pauseresume(switch_core_session_t *session, int pause)
  {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug)
    {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "fork_session_pauseresume failed because no bug\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);

    if (!tech_pvt)
      return SWITCH_STATUS_FALSE;

    switch_core_media_bug_flush(bug);
    tech_pvt->audio_paused = pause;
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_graceful_shutdown(switch_core_session_t *session)
  {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "begin fork_session_graceful_shutdown!\n");

    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug)
    {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "fork_session_graceful_shutdown failed because no bug\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);

    if (!tech_pvt)
      return SWITCH_STATUS_FALSE;

    tech_pvt->graceful_shutdown = 1;

    AudioPipe *pAudioPipe = static_cast<AudioPipe *>(tech_pvt->pAudioPipe);
    if (pAudioPipe)
      pAudioPipe->do_graceful_shutdown();

    return SWITCH_STATUS_SUCCESS;
  }

  switch_bool_t fork_frame_replace(switch_core_session_t *session, switch_media_bug_t *bug)
  {
    /* Get private data we assign to bug before at fork_init*/
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "begin fork_frame_replace!\n");

    private_t *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);
    if(!tech_pvt || (tech_pvt && tech_pvt->isClosing == 1)){
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "begin fork_frame_replace return cause closing!\n");
      return SWITCH_TRUE;
    }
    if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS)
    {
      if (!tech_pvt->pAudioPipe)
      {
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }
      AudioPipe *pAudioPipe = static_cast<AudioPipe *>(tech_pvt->pAudioPipe);
      

      pAudioPipe->lockAudioBuffer();
      switch_frame_t *frame;
      if ((frame = switch_core_media_bug_get_read_replace_frame(bug)))
      {
        char *frame_data = (char *)frame->data;
        int frame_len = frame->datalen;
        switch_core_media_bug_set_read_replace_frame(bug, frame);
        // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_READ_REPLACE. length: %d\n",frame_len);
        if (frame->channels != 1)
        {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "nonsupport channels:%d!\n", frame->channels);
          switch_mutex_unlock(tech_pvt->mutex);
          return SWITCH_FALSE;
        }
        pAudioPipe->setAudio(frame_data, frame_len);
      }
      // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_READ_REPLACE. step 2\n");
      pAudioPipe->unlockAudioBuffer();
      switch_mutex_unlock(tech_pvt->mutex);
      // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_READ_REPLACE. step 3\n");

    }
    return SWITCH_TRUE;
  }

}
