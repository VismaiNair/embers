
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "gui/imgui.h"
#include "gui/imgui_impl_sdl2.h"
#include "gui/imgui_impl_sdlrenderer2.h"
static Uint32 dummy;
#include <SDL2/SDL_image.h>
Uint32 *Pixel(SDL_Surface *screen, const SDL_Point &point) {
    dummy = 0;
    if (point.x < 0 || point.y < 0 || point.x >= screen->w || point.y >= screen->h) return &dummy;
    return (Uint32 *)((Uint8 *)screen->pixels + point.y * screen->pitch) + point.x;
}
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
using glm::vec2;
using glm::vec3;
using glm::vec4;

using glm::mat4;
#define RGBA(r, g, b, a) SDL_MapRGBA(screen->format, r, g, b, a)
glm::vec4 perspective_divide(glm::vec4 x) {
    return {x.x/x.w, x.y/x.w, x.z/x.w, 1/x.w};
}
static mat4 perspective;

struct Input {
    Uint32 left : 1 = 0;
    Uint32 right : 1 = 0;
    Uint32 up : 1 = 0;
    Uint32 down : 1 = 0;
    Uint32 front : 1 = 0;
    Uint32 back : 1 = 0;
};
#include <algorithm>
void Rasterize(SDL_Surface *screen, float *depthbuffer, vec4 Aa, vec4 Ba, vec4 Ca, mat4 view, std::array<vec2, 3> tex, SDL_Surface *surface) {
    mat4 mvp = perspective * view;
    
    // Internal struct to carry UVs through the clipping process
    struct Vertex { vec4 pos; vec4 clip; vec2 uv; };
    Vertex v[3] = { {Aa, mvp * Aa, tex[0]}, {Ba, mvp * Ba, tex[1]}, {Ca, mvp * Ca, tex[2]} };

    auto is_inside = [](const Vertex& v) { return v.clip.z >= -v.clip.w; };
    
    auto intersect = [](Vertex a, Vertex b) {
        float da = a.clip.z + a.clip.w;
        float db = b.clip.z + b.clip.w;
        float t = da / (da - db);
        return Vertex{
            glm::mix(a.pos, b.pos, t), 
            glm::mix(a.clip, b.clip, t), 
            glm::mix(a.uv, b.uv, t) 
        };
    };

    auto Draw = [&](Vertex v0, Vertex v1, Vertex v2) {
        vec3 ap = vec3(v0.clip) / v0.clip.w;
        vec3 bp = vec3(v1.clip) / v1.clip.w;
        vec3 cp = vec3(v2.clip) / v2.clip.w;

        vec2 fA = {(ap.x + 1.0f) * 0.5f * screen->w, (1.0f - ap.y) * 0.5f * screen->h};
        vec2 fB = {(bp.x + 1.0f) * 0.5f * screen->w, (1.0f - bp.y) * 0.5f * screen->h};
        vec2 fC = {(cp.x + 1.0f) * 0.5f * screen->w, (1.0f - cp.y) * 0.5f * screen->h};

        int minX = std::max(0, (int)std::floor(std::min({fA.x, fB.x, fC.x})));
        int maxX = std::min(screen->w - 1, (int)std::ceil(std::max({fA.x, fB.x, fC.x})));
        int minY = std::max(0, (int)std::floor(std::min({fA.y, fB.y, fC.y})));
        int maxY = std::min(screen->h - 1, (int)std::ceil(std::max({fA.y, fB.y, fC.y})));

        if (minX > maxX || minY > maxY) return;

        float area = (fB.y - fC.y) * (fA.x - fC.x) + (fC.x - fB.x) * (fA.y - fC.y);
        if (std::abs(area) < 1.0f) return;

        // Optimization: Pre-calculate reciprocals
        float rw0 = 1.0f / v0.clip.w;
        float rw1 = 1.0f / v1.clip.w;
        float rw2 = 1.0f / v2.clip.w;

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                float px = (float)x + 0.5f;
                float py = (float)y + 0.5f;
                float w0 = ((fB.y - fC.y) * (px - fC.x) + (fC.x - fB.x) * (py - fC.y)) / area;
                float w1 = ((fC.y - fA.y) * (px - fC.x) + (fA.x - fC.x) * (py - fC.y)) / area;
                float w2 = 1.0f - w0 - w1;

                if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                    float interpRW = (rw0 * w0) + (rw1 * w1) + (rw2 * w2);
                    vec3 interpX = (v0.pos * w0) + (v1.pos * w1) + (v2.pos * w2);
                    
                    // Use the UVs stored in the vertices (which were clipped correctly)
                    vec2 interpUVw = (v0.uv * rw0 * w0) + 
                                     (v1.uv * rw1 * w1) + 
                                     (v2.uv * rw2 * w2);

                    vec2 UV = interpUVw / interpRW;
                    
                    int tx = std::clamp((int)(UV.x * (surface->w - 1)), 0, surface->w - 1);
                    int ty = std::clamp((int)(UV.y * (surface->h - 1)), 0, surface->h - 1);
                    float dist = (1.0/interpRW) * (1.0/interpRW);
                    if (depthbuffer[screen->w * y + x] > interpRW) continue;
                    if ( *Pixel(surface, {tx, ty}) < 6) continue;
                    depthbuffer[screen->w * y + x] =  interpRW;
                    *(Pixel(screen, {x, y})) = *Pixel(surface, {tx, ty});
                }
            }
        }
    };

    // Sutherland-Hodgman for Near Plane
    Vertex output[4];
    int out_count = 0;
    for (int i = 0; i < 3; i++) {
        Vertex& cur = v[i];
        Vertex& next = v[(i + 1) % 3];
        if (is_inside(cur)) {
            if (is_inside(next)) {
                output[out_count++] = next;
            } else {
                output[out_count++] = intersect(cur, next);
            }
        } else if (is_inside(next)) {
            output[out_count++] = intersect(cur, next);
            output[out_count++] = next;
        }
    }

    if (out_count == 3) {
        Draw(output[0], output[1], output[2]);
    } else if (out_count == 4) {
        Draw(output[0], output[1], output[2]);
        Draw(output[0], output[2], output[3]);
    }
}
#define RenderQuad(Texture, a, b, c, d, view) {Rasterize(screen, depthbuffer, a, b, c, view, {vec2(0.0f, 0.0f), vec2(1.0f, 0.0f), vec2(0.0f, 1.0f)}, Texture);\
Rasterize(screen, depthbuffer, d, b, c, view, {vec2(1.0f, 1.0f), vec2(1.0f, 0.0f), vec2(0.0f, 1.0f)}, Texture);}
vec3 GetWorldPos(int mouseX, int mouseY, float targetY, int screenW, int screenH, mat4 view, mat4 projection) {
    
    float x = (2.0f * mouseX) / screenW - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenH;

    mat4 invVP = glm::inverse(projection * view);
    
    vec4 near4 = invVP * vec4(x, y, -1.0, 1.0);
    vec3 rayOrigin = vec3(near4) / near4.w;
    
    vec4 far4 = invVP * vec4(x, y, 1.0, 1.0);
    vec3 rayDest = vec3(far4) / far4.w;

    vec3 rayDir = glm::normalize(rayDest - rayOrigin);
    if (std::abs(rayDir.y) < 1e-6) return vec3(0);

    float t = (targetY - rayOrigin.y) / rayDir.y;

    // 4. Resulting 3D point
    return rayOrigin + t * rayDir;
}
struct Particles {
    vec3 position;
    vec3 velocity;
    float delay = 0;
    int ShallDelete;
    float timer;

};
int main() {
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);
    size_t Ww = 960, Hh = 640;
    int global_mouseX, global_mouseY;
    SDL_GetGlobalMouseState(&global_mouseX, &global_mouseY);

    SDL_Point pt = SDL_Point{global_mouseX, global_mouseY};
    int monitorIndex = SDL_GetPointDisplayIndex(&pt);

    
    SDL_DisplayMode mode;
    SDL_GetCurrentDisplayMode(monitorIndex, &mode);
    SDL_Window *window = SDL_CreateWindow("WILDFIRE", SDL_WINDOWPOS_CENTERED_DISPLAY(monitorIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(monitorIndex), Ww, Hh ,SDL_WINDOW_FULLSCREEN_DESKTOP);

    int WidthWindow, HeightWindow;
    SDL_GetWindowSizeInPixels(window, &WidthWindow, &HeightWindow);
    float aspect_ratio = (float)WidthWindow / HeightWindow;
    int H = 640;
    SDL_Surface *screenA = SDL_CreateRGBSurfaceWithFormat(0, WidthWindow, HeightWindow, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface *screen = SDL_CreateRGBSurfaceWithFormat(0, H * aspect_ratio, H, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Event event;
    perspective = glm::perspective<float>(glm::radians(60.0f), aspect_ratio, 0.01, 4000);
    Input input;
    int start = 0, end = 0;
    float dt;  
    mat4 view = glm::identity<mat4>();
    float frictionA = 0.8;
    float frictionB = 0.3;
    SDL_Surface *(loaded[]) = {IMG_Load("grass.png"), IMG_Load("white.png"), IMG_Load("click_real_real.png"), IMG_Load("dead.png"), IMG_Load("click.png"), SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_RGBA32)};
    for (size_t i = 0; i < sizeof(loaded)/sizeof(SDL_Surface *); i++) {
        SDL_Surface *news = SDL_ConvertSurfaceFormat(loaded[i], SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded[i]);
        loaded[i] = news;
        Uint8 r,g,b,a;
        SDL_GetRGBA(*Pixel(news, {0, 0}), screen->format, &r, &g, &b, &a);
        printf("%hhu, %hhu, %hhu, %hhu\n", r, g, b, a);
    }
    float* depthbuffer = new float[screen->w * screen->h];

   SDL_Renderer * renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
   SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, WidthWindow, HeightWindow);


    // Initialize ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Optional
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Al
    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    // Setup Style (Dark mode is classic for editors)
    ImGui::StyleColorsDark();
    
    ImGuiStyle& style = ImGui::GetStyle();
    enum {MODE_FIRE} particle_mode;
    std::vector<Particles> particles;
    float BLOCKSTATE[4096];
    for (int i = 0; i < 4096; i++) BLOCKSTATE[i] = 1.0;
    for (int i = 0; i < 4096; i++) {
        if (rand() < RAND_MAX/4) BLOCKSTATE[i] = 5.0;
    }
    vec3 wind = vec3(0, -98.0, 0.0);
    vec3 camera = vec3(0.0f, 0.0f, 0.0f);
    vec3 rotation = vec3(0.0f, 0.0f, 0.0f);
    float fire_factor = 1.0f;
    float Ftimer = 10.0f;
    float humidity = 0.5;
    float heat = 0.0;

    float trees_percent = 0.25f;
    float dead_percent = 0.0f;
    float RandFact = 10.0f;
    while (1) {
        start = end;
        end = SDL_GetPerformanceCounter();
        dt = (end - start) / (float)SDL_GetPerformanceFrequency();


        SDL_FillRect(screen, NULL, RGBA(100, 100, 100, 255));
        int MD = 0;
        int button = 0;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                SDL_DestroyWindow(window);
                SDL_FreeSurface(screen);
                for (size_t i = 0; i < sizeof(loaded)/sizeof(SDL_Surface *); i++) SDL_FreeSurface(loaded[i]);
                SDL_Quit();
                IMG_Quit();
                return 0;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_a) input.left = 1;
                if (event.key.keysym.sym == SDLK_d) input.right = 1;
                if (event.key.keysym.sym == SDLK_w) input.front = 1;
                if (event.key.keysym.sym == SDLK_s) input.back = 1;
                if (event.key.keysym.sym == SDLK_SPACE) input.up = 1;
                if (event.key.keysym.sym == SDLK_LSHIFT) input.down = 1;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                button = event.button.button;
                MD = 1;
            }
            if (event.type == SDL_MOUSEMOTION) {
                Uint32 MouseState = SDL_GetMouseState(NULL, NULL);
                if (MouseState && SDL_BUTTON_RIGHT) {
                    rotation.x += event.motion.xrel/400.0f;
                    rotation.y -= event.motion.yrel/400.0f;
                }
            }
            if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_a) input.left = 0;
                if (event.key.keysym.sym == SDLK_d) input.right = 0;
                if (event.key.keysym.sym == SDLK_w) input.front = 0;
                if (event.key.keysym.sym == SDLK_s) input.back = 0;
                if (event.key.keysym.sym == SDLK_SPACE) input.up = 0;
                if (event.key.keysym.sym == SDLK_LSHIFT) input.down = 0;
            }
        }
        
        vec4 dir = vec4((input.right - input.left), 
                        input.up - input.down, 
                        -(input.front - input.back), 
                        0.0f);

        mat4 rotY = glm::rotate(glm::mat4(1.0f), rotation.y, vec3(0, 1, 0));
        vec3 moveWorld = vec3(rotY * dir);

        camera += moveWorld * dt * 4.0f;

        view = glm::identity<mat4>();

        view = glm::rotate(view, -rotation.y, vec3(0, 1, 0));
        view = glm::translate(view, -camera);
        SDL_LockSurface(screen);
        int size = 64;
        for (int i = 0; i < screen->w * screen->h; i++) depthbuffer[i] = 0;
        Rasterize(screen, depthbuffer, {-size/2, 0, -size/2, 1}, {-size/2, 0, size/2, 1}, {size/2, 0, -size/2, 1},view, {vec2(0.0f, 0.0f), vec2(0.0f, 1.0f), vec2(1.0f, 0.0f)}, loaded[0]);
          
        Rasterize(screen, depthbuffer, {size/2, 0, size/2, 1}, {-size/2, 0, size/2, 1}, {size/2, 0, -size/2, 1},view, {vec2(1.0f, 1.0f), vec2(0.0f, 1.0f), vec2(1.0f, 0.0f)}, loaded[0]);
          
          
        for (int x = -size/2; x < size/2; x++) {
            for (int z = -size/2; z < size/2; z++) {
                vec4 pos = { (float)x, 0.0f, (float)-z, 1.0f }; 
                vec4 world_space = view * pos;
                if (world_space.z > 0.01f) continue;
                vec4 clipSpace = perspective * world_space;
        
                vec3 ndc = perspective_divide(clipSpace);

                int screenX = (int)((ndc.x + 1.0f) * 0.5f * screen->w);
                int screenY = (int)((1.0f - ndc.y) * 0.5f * screen->h);
                SDL_Point point = {screenX, screenY};
                Uint32 *pixel = Pixel(screen, point);
                *pixel = 0xFFFFFFFF;
                pos.y += 0.01;
                RenderQuad(loaded[1], pos, pos+vec4(1.0f, 0.f, 0.f, 0.f), pos+vec4(0.f, 0.f, 1.f, 0.f), pos+vec4(1.f, 0.f, 1.f, 0.f), view);
                
                if (BLOCKSTATE[(x + size/2) * 64 + (z + size/2)] < 0.01) RenderQuad(loaded[3], pos, pos+vec4(1.0f, 0.f, 0.f, 0.f), pos+vec4(0.f, 0.f, 1.f, 0.f), pos+vec4(1.f, 0.f, 1.f, 0.f), view);
                
                if (BLOCKSTATE[(x + size/2) * 64 + (z + size/2)] > 1) {
                    pos.y += 4;
                    RenderQuad(loaded[4], pos, pos+vec4(2.0f, 0.f, 0.f, 0.f), pos+vec4(0.f, -4.f, 0.f, 0.f), pos+vec4(2.f, -4.f, 0.f, 0.f), view);
                    
                }
            
               
            }
        }
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        vec4 pos = vec4(GetWorldPos(mouseX, mouseY, 0.0f, WidthWindow, HeightWindow, view, perspective), 1.0f);
        pos.y += 0.01;
        Uint32 *pxa = Pixel(screenA, {mouseX, mouseY});
        Uint8 r, g, b, a;
        SDL_GetRGBA(*pxa, screenA->format, &r, &g, &b, &a);
        if (particle_mode == MODE_FIRE && MD && button == SDL_BUTTON_LEFT && (view * vec4(0, 0, 0, 0)).y != 1 && r != 100) {
            particles.push_back((Particles){vec4(pos.x, pos.y+1, -pos.z, 1.0), vec3(0.0f, 0.f, 0.f), 0, 0, Ftimer});
            
            
        } 
        std::vector<Particles> adder;
        for (Particles &P : particles) {
            P.position += P.velocity * dt;
            P.velocity += wind * dt + dt * RandFact * vec3(rand()/(float(RAND_MAX)) - 0.5,rand()/(float(RAND_MAX)) - 0.5,rand()/(float(RAND_MAX)) - 0.5);
            
            if (P.timer < 0) {
                P.ShallDelete = 1;
            }
            if (P.position.x >= -32 && P.position.z >= -32 && P.position.x < 31 && P.position.z < 31) {float *tile_on = &BLOCKSTATE[(int(P.position.x)+32) * 64 + (int(P.position.z)+33)];
            *tile_on -= heat * 0.002 * dt;
            P.delay -= dt;
            if (*tile_on > 1) {
                P.velocity.x = 0;
                P.velocity.z = 0;
            } else {
                P.timer -= dt;
            }
            if (*tile_on < 0) P.ShallDelete = 1;
            if (*tile_on > 1 && P.position.y < 2) {
                P.position.y = 2;
                P.velocity.y = 30;
                
                P.delay -= dt;

            }
            if (P.position.y < 0) {
                P.position.y = 0;
                P.velocity.y = 0;
                P.velocity *= (1.0-frictionA);
                
                if (*tile_on > 0.01 && P.delay < 0) {
                    P.delay = (0.99 - heat) * 0.4;
                    Particles Pcpy = P;
                    Pcpy.position += vec3(rand()/float(RAND_MAX) * fire_factor, 0, rand()/float(RAND_MAX) * fire_factor)-vec3(0.5f, 0.0f, 0.5f);
                    Pcpy.velocity.y += 30 + rand()/(float)RAND_MAX * 10;
                    Pcpy.timer = Ftimer;
                    adder.push_back(Pcpy);

                }
            }
            }
            vec4 pos = { (float)P.position.x, float(P.position.y), (float)-P.position.z, 1.0f }; 
            
            P.velocity *= (1.0-frictionB);
            vec4 vp = view * pos;
            *(Pixel(loaded[5], {0, 0})) = RGBA(255,SDL_clamp(vp.z, 0, 10) * 25, 0, 255);
            RenderQuad(loaded[5], pos, pos + vec4(0.5, 0.0, 0.0, 0.0), pos + vec4(0.0, 0.5, 0.0, 0.0) , pos + vec4(0.5, 0.5, 0.0, 0.0), view);
            
        }
        for (Particles& P: adder) {
            particles.push_back(P);
        }
        particles.erase(
            std::remove_if(particles.begin(), particles.end(),
                [](const Particles& item) { return item.ShallDelete; }),
            particles.end()
        );
        pos.x = floor(pos.x);
        pos.z = floor(pos.z); 

        RenderQuad(loaded[2], pos, pos+vec4(1.0f, 0.f, 0.f, 0.f), pos+vec4(0.f, 0.f, 1.f, 0.f), pos+vec4(1.f, 0.f, 1.f, 0.f), view);
       

        SDL_UnlockSurface(screen);

        
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        // Create a simple window
        ImGui::Begin("Data");
        ImGui::SliderFloat3("wind [gravity counts as wind here]", glm::value_ptr(wind), -500.0f, 500.0f);
        ImGui::SliderFloat("Friction in Air", &frictionB, 0.0f, 1.0f);
        ImGui::SliderFloat("Friction in Ground (Compounds with Air)", &frictionA, 0.0f, 1.0f);
        ImGui::SliderFloat("Spread of Fire Multipler (m)", &fire_factor, 0.0f, 5.0f);
        ImGui::SliderFloat("Humidity (Islas %)", &humidity, 0.0f, 1.0f);
        ImGui::SliderFloat("Temperature (Degrees Bergsagels)", &heat, 0.0f, 1.0f);
        ImGui::SliderFloat("Fire Motion Randomnness", &RandFact, 0.0f, 100.0f);
        
        if (ImGui::Button("Click Me to reset Factors")) {
            wind = vec3(0, -98, 0);
            frictionB = 0.3;
            frictionA = 0.8;
            fire_factor = 1.0;
            humidity = 0.5;
            heat = 0.0;
            RandFact = 10.0;

        }
        ImGui::Separator();
        ImGui::Separator();
        ImGui::Separator();
        ImGui::SliderFloat("Tree Spawn Percent", &trees_percent, 0.0f, 1.0f);
        ImGui::SliderFloat("Non Lush Percent (Takes Priority over Trees)", &dead_percent, 0.0f, 1.0f);
        
        Ftimer = (1-humidity) * 15;
        
        if (ImGui::Button("Click Me to Regenerate World (Deletes Fire)")) {
            particles.clear();
            
            for (int i = 0; i < 4096; i++) BLOCKSTATE[i] = 1.0;
            for (int i = 0; i < 4096; i++) {
                if (rand() < RAND_MAX * trees_percent) BLOCKSTATE[i] = 5.0;
                if (rand() < RAND_MAX * dead_percent) BLOCKSTATE[i] = 0.0;
            }

        }
        if (ImGui::Button("Quit ")) {
            exit(0);

        }
        
        ImGui::End();


        ImGui::Render();
        

        //



        SDL_BlitScaled(screen, NULL, screenA, NULL);
        SDL_RenderClear(renderer);
        SDL_UpdateTexture(texture, NULL, screenA->pixels, screenA->pitch);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        

    }
}