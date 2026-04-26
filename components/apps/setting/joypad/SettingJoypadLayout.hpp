#pragma once

namespace jc4880::joypad_layout {

struct Rect {
    int x;
    int y;
    int width;
    int height;
};

struct Circle {
    int center_x;
    int center_y;
    int size;
};

struct Point {
    int x;
    int y;
};

struct Shape {
    const char *type;
    int corner_radius;
    int rotation_degrees;
    int point_count;
    Point points[16];
};

struct Visual {
    bool enabled;
    const char *label;
    const char *fill_color;
    const char *border_color;
    int border_width;
    const char *text_color;
    int text_size;
    const char *text_style;
    const char *function_type;
    int preview_analog_level;
    int preview_dpad_x;
    int preview_dpad_y;
    Shape shape;
};

struct Layout {
    int device_canvas_width;
    int device_canvas_height;
    Rect preview_frame;
    int controller_source_width;
    int controller_source_height;
    Rect trigger_bars[2];
    Rect shoulder_indicators[2];
    Rect stick_bases[2];
    Circle dpad_buttons[4];
    Circle face_buttons[4];
    Visual trigger_bar_visuals[2];
    Visual shoulder_visuals[2];
    Visual stick_visuals[2];
    Visual dpad_visuals[4];
    Visual face_visuals[4];
};

inline constexpr Layout kBleCalibrationLayout = {
    480,
    800,
    {40, 254, 400, 293},
    800,
    585,
    {
        {2, 19, 150, 120},
        {642, 13, 150, 120},
    },
    {
        {147, 6, 50, 50},
        {607, 7, 50, 50},
    },
    {
        {232, 220, 112, 112},
        {441, 220, 112, 112},
    },
    {
        {171, 122, 50},
        {138, 156, 50},
        {201, 158, 50},
        {168, 193, 50},
    },
    {
        {532, 156, 50},
        {582, 114, 50},
        {627, 160, 50},
        {578, 204, 50},
    },
    {
        {
        true,
        "L2",
        "#cbd5e1",
        "#2563eb",
        2,
        "#0f172a",
        14,
        "bold",
        "analog",
        72,
        0,
        0,
        {
            "custom",
            5,
            0,
            6,
            {
            {0, 39},
            {95, 38},
            {95, 48},
            {84, 51},
            {79, 61},
            {1, 62},
            }
        }
    },
        {
        true,
        "R2",
        "#cbd5e1",
        "#2563eb",
        2,
        "#0f172a",
        14,
        "bold",
        "analog",
        72,
        0,
        0,
        {
            "custom",
            5,
            0,
            6,
            {
            {0, 39},
            {95, 38},
            {95, 62},
            {14, 62},
            {10, 52},
            {1, 50},
            }
        }
    },
    },
    {
        {
        true,
        "L1",
        "#e2e8f0",
        "#94a3b8",
        2,
        "#0f172a",
        14,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            17,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "R1",
        "#e2e8f0",
        "#94a3b8",
        2,
        "#0f172a",
        14,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            17,
            0,
            0,
            {
            }
        }
    },
    },
    {
        {
        true,
        "LS",
        "#38bdf8",
        "#0284c7",
        2,
        "#0f172a",
        16,
        "bold",
        "dpad",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "RS",
        "#38bdf8",
        "#0284c7",
        2,
        "#0f172a",
        16,
        "bold",
        "dpad",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
    },
    {
        {
        true,
        "U",
        "#e2e8f0",
        "#0f766e",
        2,
        "#0f172a",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "custom",
            18,
            0,
            8,
            {
            {51, 9},
            {78, 10},
            {96, 50},
            {73, 81},
            {50, 98},
            {26, 80},
            {4, 50},
            {22, 10},
            }
        }
    },
        {
        true,
        "L",
        "#e2e8f0",
        "#0f766e",
        2,
        "#0f172a",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "custom",
            18,
            270,
            8,
            {
            {51, 9},
            {78, 10},
            {96, 50},
            {73, 81},
            {50, 98},
            {26, 80},
            {4, 50},
            {22, 10},
            }
        }
    },
        {
        true,
        "R",
        "#e2e8f0",
        "#0f766e",
        2,
        "#0f172a",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "custom",
            18,
            90,
            8,
            {
            {51, 9},
            {78, 10},
            {96, 50},
            {73, 81},
            {50, 98},
            {26, 80},
            {4, 50},
            {22, 10},
            }
        }
    },
        {
        true,
        "D",
        "#e2e8f0",
        "#0f766e",
        2,
        "#0f172a",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "custom",
            18,
            180,
            8,
            {
            {51, 9},
            {78, 10},
            {96, 50},
            {73, 81},
            {50, 98},
            {26, 80},
            {4, 50},
            {22, 10},
            }
        }
    },
    },
    {
        {
        true,
        "X",
        "#3b82f6",
        "#1d4ed8",
        2,
        "#ffffff",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "Y",
        "#f59e0b",
        "#d97706",
        2,
        "#ffffff",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "B",
        "#ef4444",
        "#b91c1c",
        2,
        "#ffffff",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "A",
        "#22c55e",
        "#15803d",
        2,
        "#ffffff",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
    },
};

inline constexpr Layout kLocalControllerLayout = {
    480,
    800,
    {40, 254, 400, 293},
    800,
    585,
    {
        {0, 36, 150, 24},
        {640, 43, 150, 24},
    },
    {
        {147, 6, 72, 34},
        {568, -2, 72, 34},
    },
    {
        {232, 220, 112, 112},
        {441, 220, 112, 112},
    },
    {
        {169, 147, 30},
        {142, 174, 30},
        {196, 174, 30},
        {169, 201, 30},
    },
    {
        {533, 173, 32},
        {582, 125, 32},
        {631, 173, 32},
        {582, 222, 32},
    },
    {
        {
        true,
        "L2",
        "#cbd5e1",
        "#2563eb",
        2,
        "#0f172a",
        14,
        "bold",
        "analog",
        72,
        0,
        0,
        {
            "roundedRect",
            22,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "R2",
        "#CBD5E1",
        "#2563EB",
        2,
        "#0F172A",
        14,
        "bold",
        "analog",
        72,
        0,
        0,
        {
            "roundedRect",
            999,
            0,
            0,
            {
            }
        }
    },
    },
    {
        {
        true,
        "L1",
        "#E2E8F0",
        "#94A3B8",
        2,
        "#0F172A",
        14,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "roundedRect",
            15,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "R1",
        "#E2E8F0",
        "#94A3B8",
        2,
        "#0F172A",
        14,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "roundedRect",
            15,
            0,
            0,
            {
            }
        }
    },
    },
    {
        {
        true,
        "LS",
        "#38BDF8",
        "#0284C7",
        2,
        "#0F172A",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "RS",
        "#38BDF8",
        "#0284C7",
        2,
        "#0F172A",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
    },
    {
        {
        true,
        "U",
        "#E2E8F0",
        "#0F766E",
        2,
        "#0F172A",
        16,
        "bold",
        "dpad",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "L",
        "#E2E8F0",
        "#0F766E",
        2,
        "#0F172A",
        16,
        "bold",
        "dpad",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "R",
        "#E2E8F0",
        "#0F766E",
        2,
        "#0F172A",
        16,
        "bold",
        "dpad",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "D",
        "#E2E8F0",
        "#0F766E",
        2,
        "#0F172A",
        16,
        "bold",
        "dpad",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
    },
    {
        {
        true,
        "X",
        "#3B82F6",
        "#1D4ED8",
        2,
        "#FFFFFF",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "Y",
        "#F59E0B",
        "#D97706",
        2,
        "#FFFFFF",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "B",
        "#EF4444",
        "#B91C1C",
        2,
        "#FFFFFF",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
        {
        true,
        "A",
        "#22C55E",
        "#15803D",
        2,
        "#FFFFFF",
        16,
        "bold",
        "tactile",
        50,
        0,
        0,
        {
            "circle",
            999,
            0,
            0,
            {
            }
        }
    },
    },
};

} // namespace jc4880::joypad_layout
