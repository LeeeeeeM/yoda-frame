cmake_minimum_required(VERSION 3.24)
project(yoda_render)

set(YOGA_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/yoga)

# 查找 Yoga 头文件和库
find_path(YOGA_INCLUDE_DIR
  NAMES yoga/Yoga.h
  # 本地的 Yoga 头文件路径 或 从 submodule 中头文件获取
  PATHS ${YOGA_ROOT_DIR} /usr/local/include
)

find_library(YOGA_LIBRARY
  NAMES yogacore
  # 本地的 Yogacore 库路径 或 从 submodule 中构建获取
  PATHS ${YOGA_ROOT_DIR}/yoga/build /usr/local/lib
)

find_path(SDL2_INCLUDE_DIR
  NAMES SDL2/SDL.h
  # 覆盖常见路径
  PATHS /usr/include /usr/local/include /opt/homebrew/include
)

find_library(SDL2_LIBRARY
  NAMES SDL2 libSDL2.dylib
  # 覆盖常见路径
  PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
)

set(GLIB_LIB_PATH "/opt/homebrew/lib")
set(GLIB_INCLUDE_PATH "/opt/homebrew/include/glib-2.0")
set(GLIB_INCLUDE_EXTRA "/opt/homebrew/lib/glib-2.0/include")  # 这个路径经常被忽略！

# 手动指定库和头文件
find_library(GLIB_LIBRARY
    NAMES glib-2.0
    PATHS ${GLIB_LIB_PATH}
    NO_DEFAULT_PATH
)

include_directories(${YOGA_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${GLIB_INCLUDE_PATH} ${GLIB_INCLUDE_EXTRA})

add_executable(main main.c)

target_link_libraries(main ${YOGA_LIBRARY} ${SDL2_LIBRARY} ${GLIB_LIBRARY})
