cmake_minimum_required(VERSION 3.24)
project(yoda_render)

set(YOGA_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/yoga)
set(QUICKJS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/slowjs)

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

set(QUICKJS_INCLUDE_DIR ${QUICKJS_ROOT_DIR}/include)
set(QUICKJS_LIBS_DIR ${QUICKJS_ROOT_DIR}/build/libs)
set(QUICKJS_SRC_DIR ${QUICKJS_ROOT_DIR}/build/src)

# 查找头文件
find_path(QUICKJS_HEADERS
  NAMES quickjs.h quickjs-libc.h
  HINTS ${QUICKJS_INCLUDE_DIR}  # 优先搜索项目内的路径
  PATHS /usr/local/include /opt/homebrew/include  # 其次搜索系统路径
  NO_DEFAULT_PATH  # 禁用默认路径搜索
)

# 查找静态库
find_library(QUICKJS_LIB
  NAMES quickjs libquickjs.a
  HINTS ${QUICKJS_SRC_DIR}  # libquickjs.a 的路径
  NO_DEFAULT_PATH
)
find_library(QJSLIBS_LIB
  NAMES qjslibs libqjslibs.a
  HINTS ${QUICKJS_LIBS_DIR}  # libqjslibs.a 的路径
  NO_DEFAULT_PATH
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

find_path(LIBUV_INCLUDE_DIR
  NAMES uv.h
  PATHS /usr/include /usr/local/include /opt/homebrew/include  # 覆盖常见路径
)

find_library(LIBUV_LIBRARY
  NAMES uv libuv.dylib
  PATHS /usr/lib /usr/local/lib /opt/homebrew/lib  # 覆盖常见路径
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

add_executable(main main.c)

include_directories(${YOGA_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${GLIB_INCLUDE_PATH} ${GLIB_INCLUDE_EXTRA} ${LIBUV_INCLUDE_DIR} ${QUICKJS_HEADERS})

target_link_libraries(main ${YOGA_LIBRARY} ${SDL2_LIBRARY} ${GLIB_LIBRARY} ${LIBUV_LIBRARY} ${QUICKJS_LIB} ${QJSLIBS_LIB})
