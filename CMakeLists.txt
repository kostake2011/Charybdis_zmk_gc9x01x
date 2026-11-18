if(TARGET app)
  # behavior_rgb_local removed — no RGB underglow on this build
endif()
target_sources_ifdef(CONFIG_DONGLE_ROUND_SCREEN app PRIVATE src/status_screen.c)
