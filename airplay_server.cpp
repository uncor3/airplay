#include "rpiplay.cpp"

extern "C" {
    int start_server_qt(const char* name) {
        std::vector<char> hw_addr = DEFAULT_HW_ADDRESS;
        video_renderer_config_t video_config;
        video_config.background_mode = DEFAULT_BACKGROUND_MODE;
        video_config.low_latency = DEFAULT_LOW_LATENCY;
        video_config.rotation = DEFAULT_ROTATE;
        video_config.flip = DEFAULT_FLIP;
        
        audio_renderer_config_t audio_config;
        audio_config.device = DEFAULT_AUDIO_DEVICE;
        audio_config.low_latency = DEFAULT_LOW_LATENCY;
        
        // Use Qt renderer
        video_init_func = video_renderer_qt_init;
        audio_init_func = audio_renderer_dummy_init;
        
        return start_server(hw_addr, std::string(name), false, 
                          &video_config, &audio_config, 1920, 1080, 60.0);
    }
    
    int stop_server_qt() {
        running = false;
        return stop_server();
    }
}
