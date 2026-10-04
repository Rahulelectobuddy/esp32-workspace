#!/usr/bin/env zsh
# ESP32 Project Generator Script
# Usage: ./create-project.sh <project_name>

if [ -z "$1" ]; then
    echo "Usage: ./create-project.sh <project_name>"
    exit 1
fi

PROJECT_NAME="$1"

if [ -d "$PROJECT_NAME" ]; then
    echo "Error: Directory '$PROJECT_NAME' already exists!"
    exit 1
fi

mkdir -p "$PROJECT_NAME/main"

# Create root CMakeLists.txt
cat <<EOF > "$PROJECT_NAME/CMakeLists.txt"
cmake_minimum_required(VERSION 3.16)

include(\$ENV{IDF_PATH}/tools/cmake/project.cmake)
project($PROJECT_NAME)
EOF

# Create main/CMakeLists.txt
cat <<EOF > "$PROJECT_NAME/main/CMakeLists.txt"
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ".")
EOF

# Create main/main.c boilerplate
cat <<EOF > "$PROJECT_NAME/main/main.c"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "$PROJECT_NAME";

void app_main(void)
{
    ESP_LOGI(TAG, "Hello from %s!", "$PROJECT_NAME");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
EOF

# Create .gitignore for project
cat <<EOF > "$PROJECT_NAME/.gitignore"
build/
sdkconfig
sdkconfig.old
EOF

chmod +x "$PROJECT_NAME"
echo "Project '$PROJECT_NAME' created successfully!"
echo "To build and flash:"
echo "  source env.sh"
echo "  cd $PROJECT_NAME"
echo "  idf.py build"
echo "  idf.py -p /dev/cu.usbserial-XXXX flash monitor"
