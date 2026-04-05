idf_component_register(
    SRCS "main.c"
         "beacon_scanner.c"
         "audio_player.c"
         "clip_storage.c"
         "vl53l1x.c"
         "sensor_task.c"
         "distance_categorizer_wrapper.cpp"
         "../../src/core/distance_categorizer.cpp"
    INCLUDE_DIRS "."
                 "../../src/core"
)
