#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#define MAX_WIDTH 400  // 换行宽度

int main(int argc, char* argv[]) {
    // 初始化 SDL 和 TTF
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        SDL_Log("TTF_Init failed: %s", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    // 创建窗口和渲染器
    SDL_Window* window = SDL_CreateWindow(
        "Text Wrapping Example",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640, 480,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // 加载字体
    TTF_Font* font = TTF_OpenFont("SimKai.ttf", 24);
    if (!font) {
        SDL_Log("TTF_OpenFont failed: %s", TTF_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // 要渲染的文本
    const char* text = "This is a long text example that requires automatic line wrapping based on width. The text will automatically wrap according to the window's width to ensure the content does not overflow.";

    // 设置文字颜色
    SDL_Color color = {255, 255, 255};  // 白色

    // 使用 TTF_RenderText_Blended_Wrapped 渲染文本
    SDL_Surface* text_surface = TTF_RenderText_Blended_Wrapped(font, text, color, MAX_WIDTH);
    if (!text_surface) {
        SDL_Log("TTF_RenderText_Blended_Wrapped failed: %s", TTF_GetError());
        TTF_CloseFont(font);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // 创建纹理
    SDL_Texture* text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
    if (!text_texture) {
        SDL_Log("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
        SDL_FreeSurface(text_surface);
        TTF_CloseFont(font);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // 设置文字位置
    SDL_Rect text_rect = {
        100,  // x 坐标
        100,  // y 坐标
        text_surface->w,
        text_surface->h
    };

    // 清屏
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // 绘制文字
    SDL_RenderCopy(renderer, text_texture, NULL, &text_rect);

    // 更新屏幕
    SDL_RenderPresent(renderer);

    // 事件循环：直到用户点击关闭按钮
    SDL_Event event;
    int quit = 0;
    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {  // 点击关闭按钮时退出
                quit = 1;
            }
        }
        // 添加轻微延迟减少CPU占用（可选）
        SDL_Delay(10);
    }

    // 清理资源
    SDL_DestroyTexture(text_texture);
    SDL_FreeSurface(text_surface);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}