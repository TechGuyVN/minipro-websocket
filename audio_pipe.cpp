/**
 * Modify by viethq@vihat.vn
 */
#include "audio_pipe.hpp"

#include <thread>
#include <cassert>
#include <iostream>
#include <switch.h>

/* discard incoming text messages over the socket that are longer than this */
#define MAX_RECV_BUF_SIZE (5065600)
#define RECV_BUF_REALLOC_SIZE (16 * 1024)

namespace
{
  static const char *basicAuthUser = std::getenv("MOD_AUDIO_FORK_HTTP_AUTH_USER");
  static const char *basicAuthPassword = std::getenv("MOD_AUDIO_FORK_HTTP_AUTH_PASSWORD");

  static const char *requestedTcpKeepaliveSecs = std::getenv("MOD_AUDIO_FORK_TCP_KEEPALIVE_SECS");
  static int nTcpKeepaliveSecs = requestedTcpKeepaliveSecs ? ::atoi(requestedTcpKeepaliveSecs) : 55;
}

// remove once we update to lws with this helper
// static int dch_lws_http_basic_auth_gen(const char *user, const char *pw, char *buf, size_t len)
// {
//   size_t n = strlen(user), m = strlen(pw);
//   char b[128];

//   if (len < 6 + ((4 * (n + m + 1)) / 3) + 1)
//     return 1;

//   memcpy(buf, "Basic ", 6);

//   n = lws_snprintf(b, sizeof(b), "%s:%s", user, pw);
//   if (n >= sizeof(b) - 2)
//     return 2;

//   lws_b64_encode_string(b, n, buf + 6, len - 6);
//   buf[len - 1] = '\0';

//   return 0;
// }

// int AudioPipe::lws_callback(struct lws *wsi,
//                             enum lws_callback_reasons reason,
//                             void *user, void *in, size_t len)
// {

// struct AudioPipe::lws_per_vhost_data *vhd =
//       (struct AudioPipe::lws_per_vhost_data *)lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));

//   lwsl_notice("lws_callback with reason: %d \n", reason);

//   struct lws_vhost *vhost = lws_get_vhost(wsi);
//   AudioPipe **ppAp = (AudioPipe **)user;

//   switch (reason)
//   {
//   case LWS_CALLBACK_PROTOCOL_INIT:
//     vhd = (struct AudioPipe::lws_per_vhost_data *)lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct AudioPipe::lws_per_vhost_data));
//     vhd->context = lws_get_context(wsi);
//     vhd->protocol = lws_get_protocol(wsi);
//     vhd->vhost = lws_get_vhost(wsi);
//     break;

//   // case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
//   // {
//   //   AudioPipe *ap = findPendingConnect(wsi);
//   //   if (ap && ap->hasBasicAuth())
//   //   {
//   //     unsigned char **p = (unsigned char **)in, *end = (*p) + len;
//   //     char b[128];
//   //     std::string username, password;

//   //     ap->getBasicAuth(username, password);
//   //     lwsl_notice("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER username: %s, password: xxxxxx\n", username.c_str());
//   //     if (dch_lws_http_basic_auth_gen(username.c_str(), password.c_str(), b, sizeof(b)))
//   //       break;
//   //     if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_AUTHORIZATION, (unsigned char *)b, strlen(b), p, end))
//   //       return -1;
//   //   }
//   // }
//   // break;

//   case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
//     processPendingConnects(vhd);
//     processPendingDisconnects(vhd);
//     processPendingWrites();
//     break;
//   case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
//   {
//     AudioPipe *ap = findAndRemovePendingConnect(wsi);
//     if (ap)
//     {
//       ap->m_state = LWS_CLIENT_FAILED;
//       ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECT_FAIL, (char *)in);
//     }
//     else
//     {
//       lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CONNECTION_ERROR unable to find wsi %p..\n", wsi);
//     }
//   }
//   break;

//   case LWS_CALLBACK_CLIENT_ESTABLISHED:
//   {
//     AudioPipe *ap = findAndRemovePendingConnect(wsi);
//     if (ap)
//     {
//       *ppAp = ap;
//       ap->m_vhd = vhd;
//       ap->m_state = LWS_CLIENT_CONNECTED;
//       ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECT_SUCCESS, NULL);
//     }
//     else
//     {
//       lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_ESTABLISHED %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi);
//     }
//   }
//   break;
//   case LWS_CALLBACK_CLIENT_CLOSED:
//   {
//     AudioPipe *ap = *ppAp;
//     if (!ap)
//     {
//       lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CLOSED %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi);
//       return 0;
//     }
//     if (ap->m_state == LWS_CLIENT_DISCONNECTING)
//     {
//       // closed by us
//       ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECTION_CLOSED_GRACEFULLY, NULL);
//     }
//     else if (ap->m_state == LWS_CLIENT_CONNECTED)
//     {
//       // closed by far end
//       lwsl_notice("%s socket closed by far end\n", ap->m_uuid.c_str());
//       ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECTION_DROPPED, NULL);
//     }
//     ap->m_state = LWS_CLIENT_DISCONNECTED;

//     // NB: after receiving any of the events above, any holder of a
//     // pointer or reference to this object must treat is as no longer valid

//     *ppAp = NULL;
//     // delete ap;
//   }
//   break;

//   case LWS_CALLBACK_CLIENT_RECEIVE:
//   {
//     AudioPipe *ap = *ppAp;
//     if (!ap)
//     {
//       lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi);
//       return 0;
//     }

//     if (lws_frame_is_binary(wsi))
//     {
//       lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE received binary frame, discarding.\n");
//       return 0;
//     }

//     if (lws_is_first_fragment(wsi))
//     {
//       // allocate a buffer for the entire chunk of memory needed
//       assert(nullptr == ap->m_recv_buf);
//       ap->m_recv_buf_len = len + lws_remaining_packet_payload(wsi);
//       ap->m_recv_buf = (uint8_t *)malloc(ap->m_recv_buf_len);
//       ap->m_recv_buf_ptr = ap->m_recv_buf;
//     }

//     size_t write_offset = ap->m_recv_buf_ptr - ap->m_recv_buf;
//     size_t remaining_space = ap->m_recv_buf_len - write_offset;
//     if (remaining_space < len)
//     {
//       lwsl_notice("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE buffer realloc needed.\n");
//       size_t newlen = ap->m_recv_buf_len + RECV_BUF_REALLOC_SIZE;
//       if (newlen > MAX_RECV_BUF_SIZE)
//       {
//         free(ap->m_recv_buf);
//         ap->m_recv_buf = ap->m_recv_buf_ptr = nullptr;
//         ap->m_recv_buf_len = 0;
//         lwsl_notice("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE max buffer exceeded, truncating message.\n");
//       }
//       else
//       {
//         ap->m_recv_buf = (uint8_t *)realloc(ap->m_recv_buf, newlen);
//         if (nullptr != ap->m_recv_buf)
//         {
//           ap->m_recv_buf_len = newlen;
//           ap->m_recv_buf_ptr = ap->m_recv_buf + write_offset;
//         }
//       }
//     }

//     if (nullptr != ap->m_recv_buf)
//     {
//       if (len > 0)
//       {
//         memcpy(ap->m_recv_buf_ptr, in, len);
//         ap->m_recv_buf_ptr += len;
//       }
//       if (lws_is_final_fragment(wsi))
//       {
//         if (nullptr != ap->m_recv_buf)
//         {
//           std::string msg((char *)ap->m_recv_buf, ap->m_recv_buf_ptr - ap->m_recv_buf);
//           ap->m_callback(ap->m_uuid.c_str(), AudioPipe::MESSAGE, msg.c_str());
//           if (nullptr != ap->m_recv_buf)
//             free(ap->m_recv_buf);
//         }
//         ap->m_recv_buf = ap->m_recv_buf_ptr = nullptr;
//         ap->m_recv_buf_len = 0;
//       }
//     }
//   }
//   break;

//   case LWS_CALLBACK_CLIENT_WRITEABLE:
//   {
//     AudioPipe *ap = *ppAp;
//     if (!ap || (ap && !ap->m_audio_buffer))
//     {
//       lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi);
//       return 0;
//     }
//     // check for graceful close - send a zero length binary frame
//     if (ap->isGracefulShutdown())
//     {
//       lwsl_notice("%s graceful shutdown - sending zero length binary frame to flush any final responses\n", ap->m_uuid.c_str());
//       std::lock_guard<std::mutex> lk(ap->m_audio_mutex);
//       int sent = lws_write(wsi, (unsigned char *)ap->m_audio_buffer + LWS_PRE, 0, LWS_WRITE_BINARY);
//       return 0;
//     }

//     // // check for text frames to send
//     // {
//     //   std::lock_guard<std::mutex> lk(ap->m_text_mutex);
//     //   if (ap->m_metadata.length() > 0)
//     //   {
//     //     uint8_t buf[ap->m_metadata.length() + LWS_PRE];
//     //     memcpy(buf + LWS_PRE, ap->m_metadata.c_str(), ap->m_metadata.length());
//     //     int n = ap->m_metadata.length();
//     //     int m = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_TEXT);
//     //     ap->m_metadata.clear();
//     //     if (m < n)
//     //     {
//     //       return -1;
//     //     }

//     //     // there may be audio data, but only one write per writeable event
//     //     // get it next time
//     //     lws_callback_on_writable(wsi);

//     //     return 0;
//     //   }
//     // }

//     if (ap->m_state == LWS_CLIENT_DISCONNECTING)
//     {
//       lwsl_notice("%s begin LWS_CLOSE_STATUS_NORMAL\n", ap->m_uuid.c_str());
//       lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
//       return -1;
//     }

//     // check for audio packets
//     {
//       std::lock_guard<std::mutex> lk(ap->m_audio_mutex);
//       if (ap->m_audio_buffer)
//       {
//         // LWS have to have LWS_PRE byte brfore for Lws use, after that fill data
//         uint8_t * buf = new  uint8_t[ap->m_audio_buffer_write_offset + LWS_PRE];
//         memcpy(buf + LWS_PRE, ap->m_audio_buffer, ap->m_audio_buffer_write_offset);
//         int sent = lws_write(wsi, (unsigned char *)buf + LWS_PRE, ap->m_audio_buffer_write_offset, LWS_WRITE_BINARY);
//         delete [] buf;
//         delete [] ap->m_audio_buffer;
//         ap->m_audio_buffer  = nullptr;
//         ap->m_audio_buffer_write_offset = 0;
//       }
//     }

//     return 0;
//   }
//   break;

//   default:
//     break;
//   }
//   return 0;
//   // return lws_callback_http_dummy(wsi, reason, user, in, len);
// }

// static members

// struct lws_context *AudioPipe::contexts[] = {
//     nullptr, nullptr, nullptr, nullptr, nullptr,
//     nullptr, nullptr, nullptr, nullptr, nullptr};
AudioPipe::log_emit_function AudioPipe::logger;

void AudioPipe::initialize(log_emit_function _logger)
{
  if (logger = nullptr)
  {
    logger = _logger;
  }
}

bool AudioPipe::deinitialize()
{
  return true;
}

void omi_logger(const char *line)
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

// instance members
AudioPipe::AudioPipe(const char *uuid, const char *host, const char *username, const char *password, notifyHandler_t callback) : m_uuid(uuid), m_host(host), m_callback(callback)
{

  if (username && password)
  {
    m_username.assign(username);
    m_password.assign(password);
  }
  // m_audio_buffer = new uint8_t[m_audio_buffer_max_len];
}

/* Add log for checking dispose event*/
AudioPipe::~AudioPipe()
{
}

void AudioPipe::connect(void)
{
  // addPendingConnect(this);
    try
  {
    // set logging policy if needed
    omi_logger("attempting connection to websocket");

    this->cli.clear_access_channels(websocketpp::log::alevel::frame_header);
    this->cli.clear_access_channels(websocketpp::log::alevel::frame_payload);
    // c.set_error_channels(websocketpp::log::elevel::none);

    // Initialize ASIO
    this->cli.init_asio();

    // Register our handlers
     this->cli.set_open_handler(bind(&AudioPipe::on_open,this,&this->cli,::_1));
    // this->cli.set_open_handler(websocketpp::lib::bind(&AudioPipe::on_open, &this->cli, connectionHdl, ::_1));

    this->cli.set_fail_handler(bind(&AudioPipe::on_fail,this, &this->cli, ::_1));
    this->cli.set_message_handler(bind(&AudioPipe::on_message,this, &this->cli, ::_1, ::_2));
    this->cli.set_close_handler(bind(&AudioPipe::on_close,this, &this->cli, ::_1));

    // Create a connection to the given URI and queue it for connection once
    // the event loop starts
    websocketpp::lib::error_code ec;
    client::connection_ptr con = this->cli.get_connection(this->m_host.c_str(), ec);
    this->cli.connect(con);

    // Start the ASIO io_service run loop
    this->cli.run();
  }
  catch (const std::exception &e)
  {
    std::cout << e.what() << std::endl;
  }
  catch (websocketpp::lib::error_code e)
  {
    std::cout << e.message() << std::endl;
  }
  catch (...)
  {
    std::cout << "other exception" << std::endl;
  }

}

// Handlers
void AudioPipe::on_open(client *c, websocketpp::connection_hdl hdl)
{
  // std::string msg = "Hello";
  omi_logger("socket on_open");

  // c->send(hdl,msg,websocketpp::frame::opcode::text);
  // c->get_alog().write(websocketpp::log::alevel::app, "Sent Message: "+msg);
  this->m_callback(this->m_uuid.c_str(), AudioPipe::CONNECT_SUCCESS, NULL);
}

void AudioPipe::on_fail(client *c, websocketpp::connection_hdl hdl)
{
  char mes[] = "socket on_fail!";
  omi_logger(mes);

  // c->get_alog().write(websocketpp::log::alevel::app, "Connection Failed");
  this->m_callback(m_uuid.c_str(), AudioPipe::CONNECT_FAIL, mes);
}

void AudioPipe::on_message(client *c, websocketpp::connection_hdl hdl, message_ptr msg)
{
  // c->get_alog().write(websocketpp::log::alevel::app, "Received Reply: "+msg->get_payload());

  this->m_callback(m_uuid.c_str(), AudioPipe::MESSAGE, msg->get_payload().c_str());
  // c->close(hdl,websocketpp::close::status::normal,"");
}

void AudioPipe::on_close(client *c, websocketpp::connection_hdl hdl)
{
  c->get_alog().write(websocketpp::log::alevel::app, "Connection Closed");
  char mes[] = "socket on_close!";
  omi_logger(mes);

  this->m_callback(m_uuid.c_str(), AudioPipe::CONNECTION_CLOSED_GRACEFULLY, NULL);
}



void AudioPipe::unlockAudioBuffer()
{
  m_audio_mutex.unlock();
}

void AudioPipe::close()
{
  
}

void AudioPipe::do_graceful_shutdown()
{
  m_gracefulShutdown = true;
}
