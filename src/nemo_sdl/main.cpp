#include <libnes/ppu.hpp>
#include <libnes/cpu.hpp>
#include <libnes/clock.hpp>
#include <libnes/literals.hpp>
#include <libnes/console.hpp>

#include <SDL2/SDL.h>

//#include <imgui/imgui_impl_sdl.h>

#include <stdexcept>
#include <array>
#include <random>
#include <cassert>
#include <memory>
#include <unordered_map>
#include <fstream>
#include <tuple>
#include <string>
#include <deque>
#include <numeric>
#include "icon16.hpp"

using namespace nes::literals;
using namespace std::string_literals;

namespace sdl
{

class window {
public:
    virtual ~window() {
        SDL_DestroyWindow(window_);
    }

    void process_event(const SDL_Event e) {
        assert(e.type == SDL_WINDOWEVENT);
        assert(SDL_GetWindowID(window_) == e.window.windowID);

        switch(e.window.event) {
            case SDL_WINDOWEVENT_CLOSE:
                close();
        }
    }

    virtual void close() {
        SDL_HideWindow(window_);
    }

    [[nodiscard]] auto id() const { return SDL_GetWindowID(window_); }
    [[nodiscard]] auto quit() const { return quit_; }

protected:
    bool quit_{false};
    SDL_Window* window_{nullptr};
};

class frontend
{
    frontend()  { SDL_Init(SDL_INIT_EVERYTHING); }

public:
    ~frontend() { SDL_Quit(); }
    frontend(frontend&&) = default;
    frontend& operator=(frontend&&) = default;

    auto static create() { return frontend{}; }

    auto process_window_event(const SDL_Event& e) {
        auto found = windows_.find(e.window.windowID);
        if (found == std::end(windows_))
            return false;

        auto window = found->second;
        window->process_event(e);
        return window->quit();
    }

    auto process_events() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type)
            {
                case SDL_WINDOWEVENT:
                    if (process_window_event(e))
                        return true;
                    break;
                case SDL_QUIT: return true;
                case SDL_KEYDOWN: break;
            }
        }
        return false;
    }

    void add_window(window* w) {
        windows_.insert(std::pair{w->id(), w});
    }

private:
    std::unordered_map<std::uint32_t, window*> windows_;
};

class main_window: public window
{
public:
    main_window(std::string title, std::string filename)
        : title_{std::move(title) + " | " + std::move(filename)}
    {
        window_ = SDL_CreateWindow(title_.c_str() , SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 600, 480, SDL_WINDOW_OPENGL);
        if (window_ == nullptr)
            throw std::runtime_error("Cannot create window");

        glcontext_ = SDL_GL_CreateContext(window_);

//        // Setup Dear ImGui context
//        IMGUI_CHECKVERSION();
//        ImGui::CreateContext();
//        ImGuiIO &io = ImGui::GetIO();
//        // Setup Platform/Renderer bindings
//        ImGui_ImplGlfw_InitForOpenGL(window, true);
//        ImGui_ImplOpenGL3_Init(glsl_version);
//        // Setup Dear ImGui style
//        ImGui::StyleColorsDark();

        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
        if (renderer_ == nullptr)
            throw std::runtime_error("Cannot create renderer");

        screen_ = SDL_CreateTexture (renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 240);

        std::uint32_t rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        int shift = (my_icon.bytes_per_pixel == 3) ? 8 : 0;
        rmask = 0xff000000 >> shift;
        gmask = 0x00ff0000 >> shift;
        bmask = 0x0000ff00 >> shift;
        amask = 0x000000ff >> shift;
#else // little endian, like x86
        rmask = 0x000000ff;
        gmask = 0x0000ff00;
        bmask = 0x00ff0000;
        amask = (icon16::gimp_image.bytes_per_pixel == 3) ? 0 : 0xff000000;
#endif
        SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(
            (void*)icon16::gimp_image.pixel_data,
            icon16::gimp_image.width,
            icon16::gimp_image.height,
            icon16::gimp_image.bytes_per_pixel*8,
            icon16::gimp_image.bytes_per_pixel*icon16::gimp_image.width,
            rmask, gmask, bmask, amask);

        SDL_SetWindowIcon(window_, icon);
        SDL_FreeSurface(icon);
    }

    void display_fps(double fps) {
        fps_.push_back(fps);
        if (fps_.size() > 100) {
            fps_.pop_front();
        }
        auto average_fps = std::accumulate(fps_.begin(), fps_.end(), 0.0) / fps_.size();
        auto title = title_ + " | " + std::to_string(average_fps).substr(0,6) + " fps"s;
        SDL_SetWindowTitle(window_, title.c_str());
    }

    void render(const auto& frame_buffer) {
        SDL_RenderClear(renderer_);
        SDL_UpdateTexture(screen_, nullptr, frame_buffer.data(), 256 * sizeof(std::uint32_t));
        SDL_RenderCopy(renderer_, screen_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }

    void close() override {
        quit_ = true;
    }

    ~main_window() override {
        SDL_DestroyTexture(screen_);
        SDL_DestroyRenderer(renderer_);
    }

private:
    SDL_Renderer* renderer_{nullptr};
    SDL_Texture* screen_{nullptr};

    SDL_GLContext glcontext_;

    std::deque<float> fps_;

    std::string title_;
};

class chr_window: public window
{
public:
    chr_window(const char* title) {
        window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 256, 256, SDL_WINDOW_UTILITY);
        if (window_ == nullptr)
            throw std::runtime_error("Cannot create window");

        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
        if (renderer_ == nullptr)
            throw std::runtime_error("Cannot create renderer");

        chr_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128);
    }

    void render(const auto& pattern_table) {
        SDL_RenderClear(renderer_);
        SDL_UpdateTexture(chr_, nullptr, pattern_table.data(), 128 * sizeof(std::uint32_t));
        SDL_RenderCopy(renderer_, chr_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }

    ~chr_window() override {
        SDL_DestroyTexture(chr_);
        SDL_DestroyRenderer(renderer_);
    }

private:
    SDL_Renderer* renderer_{nullptr};
    SDL_Texture* chr_{nullptr};
};

}

auto load_dummy_texture(auto renderer) {
    auto surface = SDL_LoadBMP("link.bmp");
    return SDL_CreateTextureFromSurface(renderer, surface);
}

auto load_texture(const auto& frame_bufer) {

}

struct screen
{
    std::array<nes::color, 256 * 240> frame_buffer;

    [[nodiscard]] constexpr static auto width() -> short { return 256; }
    [[nodiscard]] constexpr static auto height() -> short { return 240; }

    void draw_pixel(nes::point where, nes::color color) {
        if (where.x >= width() or where.y >= height())
            return;
        frame_buffer[where.y * 256 + where.x] = color;
    }
};

auto load_rom(auto filename) -> std::unique_ptr<nes::cartridge> {
    auto romfile = std::ifstream{filename, std::ifstream::binary};
    assert(romfile.is_open());

    nes::ines_header header;
    romfile.read(reinterpret_cast<char*>(&header), sizeof(header));

    auto mapper_ix = (header.mapper1 >> 4) | (header.mapper2 & 0xF0);

    assert(mapper_ix == 0 or mapper_ix == 1);

    if (mapper_ix == 0) {
        if (header.prg_rom_chunks > 2)
            throw std::runtime_error("unsupported mapper, too many PRG sections");

        if (header.chr_rom_chunks > 1)
            throw std::runtime_error("unsupported mapper, too many CHR sections");

        auto prg = std::vector<std::array<std::uint8_t, 16_Kb>>{};

        for (auto i = 0; i < header.prg_rom_chunks; ++i) {
            prg.emplace_back();
            romfile.read(reinterpret_cast<char*>(prg.back().data()), prg.back().size());
        }

        auto chr = std::array<std::uint8_t, 8_Kb>{};
        romfile.read(reinterpret_cast<char*>(chr.data()), chr.size());

        auto mirroring = (header.mapper1 & 0x01)
                         ? nes::name_table_mirroring::vertical
                         : nes::name_table_mirroring::horizontal;

        return std::make_unique<nes::nrom>(prg, chr, mirroring);
    }

    if (mapper_ix == 1) {
        auto prg = std::vector<std::array<std::uint8_t, 16_Kb>>{};

        for (auto i = 0; i < header.prg_rom_chunks; ++i) {
            prg.emplace_back();
            romfile.read(reinterpret_cast<char*>(prg.back().data()), prg.back().size());
        }

        auto chr = std::vector<std::array<std::uint8_t, 4_Kb>>{};

        for (auto i = 0; i < header.chr_rom_chunks; ++i) {
            chr.emplace_back();
            romfile.read(reinterpret_cast<char*>(chr.back().data()), chr.back().size());
        }

        return std::make_unique<nes::mmc1>(prg, chr);
    }

    throw std::runtime_error("Unsupported mapper " + std::to_string(mapper_ix));


}

static std::random_device rd;
static std::mt19937 gen(rd());

struct config
{
    std::string filename;
};

auto parse(int argc, char *argv[]) {
    if (argc < 2)
        throw std::runtime_error("No ROM file specified");

    auto filename = std::string{argv[1]};

    return config { filename };
}

int main(int argc, char *argv[]) {
    auto config = parse(argc, argv);
    auto frontend = sdl::frontend::create();

    auto window = sdl::main_window("NES Emulator", config.filename);

    auto scr = screen{};
    auto console = nes::console{load_rom("rom/"s + config.filename)};

    const auto FPS   = 60;
    const auto DELAY = static_cast<int>(1000.0f / FPS);
    std::uint32_t frameStart, frameTime;

    frontend.add_window(&window);

    for (;;) {
        frameStart = SDL_GetTicks();

        auto stop = frontend.process_events();
        if (stop)
            break;

        auto time_machine = (SDL_GetModState() & KMOD_CAPS) != 0;

        if (not time_machine) {
            auto kb_state = SDL_GetKeyboardState(nullptr);

            auto keys = std::uint8_t{0};
            if (kb_state[SDL_SCANCODE_SPACE])   keys |= 0x80;
            if (kb_state[SDL_SCANCODE_LSHIFT])  keys |= 0x40;
            if (kb_state[SDL_SCANCODE_C])       keys |= 0x20;
            if (kb_state[SDL_SCANCODE_V])       keys |= 0x10;
            if (kb_state[SDL_SCANCODE_UP])      keys |= 0x08;
            if (kb_state[SDL_SCANCODE_DOWN])    keys |= 0x04;
            if (kb_state[SDL_SCANCODE_LEFT])    keys |= 0x02;
            if (kb_state[SDL_SCANCODE_RIGHT])   keys |= 0x01;

            console.controller_input(keys);
        }

        auto [forward, backward] = []() {
            auto kb_state = SDL_GetKeyboardState(nullptr);
            return std::tuple{
                kb_state[SDL_SCANCODE_RIGHT] != 0,
                kb_state[SDL_SCANCODE_LEFT] != 0
            };
        }();

        if (time_machine and forward or not time_machine) {
            console.render_frame(scr);
        }

        window.render(scr.frame_buffer);

        frameTime = SDL_GetTicks() - frameStart;

        window.display_fps(1.0 / frameTime * 1000);
        if (frameTime < DELAY) {
            SDL_Delay((int)(DELAY - frameTime));
        }
    }

    return 0;
}