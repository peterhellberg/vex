const vex = @import("vex");

export fn update() void {
    // clear to dark blue
    vex.cls(8);

    // white text
    vex.text("VEX ZIG", 8, 8, 12);
}
