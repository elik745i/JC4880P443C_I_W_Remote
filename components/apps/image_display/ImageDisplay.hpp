#pragma once

#include <string>
#include <vector>
#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "file_iterator.h"
#include "esp_timer.h"

class AppImageDisplay: public ESP_Brookesia_PhoneApp
{
private:

    char _image_path[256];
    const char *_image_name;
    file_iterator_instance_t *_image_file_iterator;
    std::vector<std::string> _image_paths;
    


public:
    AppImageDisplay(/* args */);
    ~AppImageDisplay();

    static void image_change_cb(lv_event_t *e);
    static void image_delay_change(AppImageDisplay *app);
    
    bool run(void);
    bool pause(void);
    bool resume(void);
    bool back(void);
    bool close(void);

    bool init(void) override;

    size_t imagePathCount() const;
    const std::string &imagePathAt(size_t index) const;
    bool debugQueueOpenIndex(size_t index);
    std::string debugDescribeState() const;

    static void timer_refersh_task(void *arg);

};


