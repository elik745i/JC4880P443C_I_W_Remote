#pragma once

#if CONFIG_JC4880_APP_MUSIC_PLAYER
#include "music_player/MusicPlayer.hpp"
#endif

#if CONFIG_JC4880_APP_SETTINGS
#include "setting/Setting.hpp"
#endif

#if CONFIG_JC4880_APP_CALCULATOR
#include "calculator/Calculator.hpp"
#endif

#if CONFIG_JC4880_APP_IMAGE_VIEWER
#include "image_display/ImageDisplay.hpp"
#endif

#if CONFIG_JC4880_APP_FILE_MANAGER
#include "file_manager/FileManager.hpp"
#endif

#if CONFIG_JC4880_APP_WEB_SERVER
#include "web_server/WebServerApp.hpp"
#endif

#if CONFIG_JC4880_APP_INTERNET_RADIO
#include "internet_radio/InternetRadio.hpp"
#endif

#if CONFIG_JC4880_APP_P4_BROWSER
#include "p4_browser/P4Browser.hpp"
#endif

#if CONFIG_JC4880_APP_P4_YOUTUBE
#include "p4_youtube/P4YouTube.hpp"
#endif

#if CONFIG_JC4880_APP_EREADER
#include "e_reader/EReader.hpp"
#endif

#if CONFIG_JC4880_APP_MQTT
#include "mqtt_app/MqttApp.hpp"
#endif

#if CONFIG_JC4880_APP_SEGA_EMULATOR
#include "sega_emulator/SegaEmulator.hpp"
#endif