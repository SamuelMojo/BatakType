#include "raylib.h"

int main() {

    InitWindow(800, 600, "BatakType");
    SetTargetFPS(240);
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("BatakType: milestone 0", 20, 20, 30, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
