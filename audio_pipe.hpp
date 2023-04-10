/** 
 * Modify by viethq@vihat.vn
*/
#ifndef __AUDIO_PIPE_HPP__
#define __AUDIO_PIPE_HPP__

#include <string>
#include <list>
#include <mutex>
#include <websocketpp/config/asio_no_tls_client.hpp>

#include <websocketpp/client.hpp>

#include <iostream>

typedef websocketpp::client<websocketpp::config::asio_client> client;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

// pull out the type of messages sent by our config
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
using ConnectionHdl = websocketpp::connection_hdl;


class AudioPipe
{
public:
  enum LwsState_t
  {
    LWS_CLIENT_IDLE,
    LWS_CLIENT_CONNECTING,
    LWS_CLIENT_CONNECTED,
    LWS_CLIENT_FAILED,
    LWS_CLIENT_DISCONNECTING,
    LWS_CLIENT_DISCONNECTED
  };
  enum NotifyEvent_t
  {
    CONNECT_SUCCESS,
    CONNECT_FAIL,
    CONNECTION_DROPPED,
    CONNECTION_CLOSED_GRACEFULLY,
    MESSAGE
  };

typedef void (*log_emit_function)(const char *line);
typedef void (*notifyHandler_t)(const char *sessionId, NotifyEvent_t event, const char* message);

  // struct lws_per_vhost_data
  // {
  //   struct lws_context *context;
  //   struct lws_vhost *vhost;
  //   const struct lws_protocols *protocol;
  // };

  static void initialize(log_emit_function logger);
  static bool deinitialize();
  // static bool lws_service_thread(unsigned int nServiceThread);

  // constructor
  AudioPipe(const char *uuid, const char *host, const char *username, const char *password, notifyHandler_t callback);
  ~AudioPipe();

  void setAudio(char *buffer, int length)
  {
      std::string str(buffer);
      cli.send(connectionHdl, str, websocketpp::frame::opcode::text);
  }


  // LwsState_t getLwsState(void) { return m_state; }
  void connect(void);
  

  void lockAudioBuffer(void)
  {
    m_audio_mutex.lock();
  }
  void unlockAudioBuffer(void);
  bool hasBasicAuth(void)
  {
    return !m_username.empty() && !m_password.empty();
  }

  void getBasicAuth(std::string &username, std::string &password)
  {
    username = m_username;
    password = m_password;
  }

  void do_graceful_shutdown();
  bool isGracefulShutdown(void)
  {
    return m_gracefulShutdown;
  }

  void close();
  void on_open(client *c, websocketpp::connection_hdl hdl);
  void on_message(client *c, websocketpp::connection_hdl hdl, message_ptr msg);
  void on_close(client *c, websocketpp::connection_hdl hdl);

  void on_fail(client *c, websocketpp::connection_hdl hdl);

  // no default constructor or copying
  AudioPipe() = delete;
  AudioPipe(const AudioPipe &) = delete;
  void operator=(const AudioPipe &) = delete;

private:
  static std::string protocolName;
  static log_emit_function logger;


  // LwsState_t m_state;
  std::string m_uuid;
  std::string m_host;
  unsigned int m_port;
  std::string m_path;
  std::string m_metadata;
  std::mutex m_text_mutex;
  std::mutex m_audio_mutex;
  int m_sslFlags;
  // struct lws_per_vhost_data *m_vhd;
  notifyHandler_t m_callback;
  log_emit_function m_logger;
  std::string m_username;
  std::string m_password;
  bool m_gracefulShutdown;
  client cli;
  ConnectionHdl connectionHdl;

};

#endif
