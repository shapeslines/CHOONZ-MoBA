# moba_replay -- headless M3.0 deterministic replay recorder/inspector/verifier.
# File I/O stays at the application boundary; eng_sim remains platform-free.
add_executable(moba_replay src/main.cpp)
target_link_libraries(moba_replay PRIVATE
    eng::sim eng::platform moba_warnings moba_options)
moba_stage_asan_runtime()
