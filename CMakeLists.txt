if(TARGET app)
  target_sources(app PRIVATE src/behaviors/behavior_rgb_local.c)
endif()
target_sources_ifdef(CONFIG_DONGLE_ROUND_SCREEN app PRIVATE src/status_screen.c)
