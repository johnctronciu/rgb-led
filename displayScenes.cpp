#include "led-matrix.h"
#include "graphics.h"

#include <algorithm>
#include <fstream>
#include <streambuf>
#include <string>

#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <iostream>

using namespace std;
using namespace rgb_matrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options] [<text>| -i <filename>]\n", progname);
  fprintf(stderr, "Takes text and scrolls it with speed -s\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr,
          "\t-f <font-file>    : Path to *.bdf-font to be used.\n"
          "\t-i <textfile>     : Input from file.\n"
          "\t-s <speed>        : Approximate letters per second. \n"
          "\t                    Positive: scroll right to left; Negative: scroll left to right\n"
          "\t                    (Zero for no scrolling)\n"
          "\t-l <loop-count>   : Number of loops through the text. "
          "-1 for endless (default)\n"
          "\t-b <on-time>,<off-time>  : Blink while scrolling. Keep "
          "on and off for these amount of scrolled pixels.\n"
          "\t-x <x-origin>     : Shift X-Origin of displaying text (Default: 0)\n"
          "\t-y <y-origin>     : Shift Y-Origin of displaying text (Default: 0)\n"
          "\t-t <track-spacing>: Spacing pixels between letters (Default: 0)\n"
          "\n"
          "\t-C <r,g,b>        : Text Color. Default 255,255,255 (white)\n"
          "\t-B <r,g,b>        : Background-Color. Default 0,0,0\n"
          );
  fprintf(stderr, "\nGeneral LED matrix options:\n");
  rgb_matrix::PrintMatrixFlags(stderr);
  return 1;
}

static bool parseColor(Color *c, const char *str) {
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

static void add_micros(struct timespec *accumulator, long micros) {
  const long billion = 1000000000;
  const int64_t nanos = (int64_t) micros * 1000;
  accumulator->tv_sec += nanos / billion;
  accumulator->tv_nsec += nanos % billion;
  while (accumulator->tv_nsec > billion) {
    accumulator->tv_nsec -= billion;
    accumulator->tv_sec += 1;
  }
}

static bool FullSaturation(const Color &c) {
  return (c.r == 0 || c.r == 255)
    && (c.g == 0 || c.g == 255)
    && (c.b == 0 || c.b == 255);
}

typedef uint64_t stat_fingerprint_t;

static bool ReadLineOnChange(const char *filename, std::string *out,
                             stat_fingerprint_t *last_file_status) {
  struct stat sb;
  if (stat(filename, &sb) < 0) {
    perror("Couldn't determine file change");
    return false;
  }
  const stat_fingerprint_t fp = ((uint64_t)sb.st_mtime << 32) + sb.st_size;
  if (fp == *last_file_status) {
    return false;  // no change according to stat()
  }

  *last_file_status = fp;
  std::ifstream fs(filename);
  std::string str((std::istreambuf_iterator<char>(fs)),
                  std::istreambuf_iterator<char>());
  // std::replace(str.begin(), str.end(), '\n', ' ');
        //Commented out as I want multi lines written from the file 
  if (*out == str) {
    return false;  // no content change
  }
  *out = str;
  return true;
}

struct displayState {
  std::string filename;
  std::vector<std::string> lines;
  stat_fingerprint_t last_change = 0;
  int x;
  int y;
  int max_length = 0;
  bool horizontal_scroll;  // true = horizontal scroll, false = vertical scroll
};

static bool readLines(displayState &state) {
  std::string line;
  if (!ReadLineOnChange(state.filename.c_str(), &line, &state.last_change)) {
    return false;
  }
  printf("file: %s\n", state.filename.c_str());
  state.lines.clear();
  std::string l;
  for (char c : line) {
    if (c == '\n') {
      state.lines.push_back(l);
      l.clear();
    } else {
      l += c;
    }
  }
  if (!l.empty()) {
    state.lines.push_back(l);
  }
  return true;
}

static void DrawFrame(FrameCanvas *offscreen_canvas,
                      displayState &state,
                      const rgb_matrix::Font &font,
                      Color color, Color bg_color, int letter_spacing) {


    for (size_t i = 0; i < state.lines.size(); i++) {
        int height = state.y + (i * font.height()) + font.baseline();
        int length = rgb_matrix::DrawText(offscreen_canvas, font, state.x, height,
                            color, NULL, state.lines[i].c_str(), letter_spacing);
        state.max_length = (length > state.max_length) ? length : state.max_length;
                      
      } 
}

static bool scroll(displayState &state,
                          int scroll_direction,
                          int canvas_width,
                          const rgb_matrix::Font &font,
                          int x_orig) {
  if (state.horizontal_scroll) {
    state.x += scroll_direction;
    if ((scroll_direction < 0 && state.x + state.max_length < 0) ||
        (scroll_direction > 0 && state.x > canvas_width)) {
      state.x = x_orig + ((scroll_direction > 0) ? -state.max_length : 0);
      state.max_length = 0;
      return true;  // finished scrolling this message
    }
  } else {
    state.y += scroll_direction;
    if ((scroll_direction < 0 && state.y + (int)state.lines.size() * font.height() < 0) ||
        (scroll_direction > 0 && state.y > 64)) {
      state.max_length = 0;
      return true;
    }
  }
  return false;
}

//static

int main(int argc, char *argv[]) {
    // Create default options.
    rgb_matrix::RGBMatrix::Options matrix_options;
    rgb_matrix::RuntimeOptions runtime_opt;
    
    runtime_opt.drop_priv_user = getenv("SUDO_UID");
    runtime_opt.drop_priv_group = getenv("SUDO_GID");

    if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
                                         &matrix_options, &runtime_opt)) {
    return usage(argv[0]);
  }

  Color color(255, 255, 255);
  Color bg_color(0, 0, 0);

  const char *bdf_font_file = NULL;
  const char *input_file = NULL;
  std::string line;
  bool xorigin_configured = false;
  int x_orig = 0;
  int letter_spacing = 0;
  float speed = 7.0f;
  int loops = -1;
  int blink_on = 0;
  int blink_off = 0;

    matrix_options.rows = 64;
    matrix_options.cols = 64;
    matrix_options.chain_length = 1;
    matrix_options.parallel = 1;
    matrix_options.hardware_mapping = "adafruit-hat";
    matrix_options.brightness=75;

  int opt;
  while ((opt = getopt(argc, argv, "x:y:f:C:B:O:t:s:l:b:i:")) != -1) {
    switch (opt) {
    case 's': speed = atof(optarg); break;
    case 'b':
      if (sscanf(optarg, "%d,%d", &blink_on, &blink_off) == 1) {
        blink_off = blink_on;
      }
      fprintf(stderr, "hz: on=%d off=%d\n", blink_on, blink_off);
      break;
    case 'l': loops = atoi(optarg); break;
    case 'x': x_orig = atoi(optarg); xorigin_configured = true; break;
    case 'f': bdf_font_file = strdup(optarg); break;
    case 'i': input_file = strdup(optarg); break;
    case 't': letter_spacing = atoi(optarg); break;
    case 'C':
      if (!parseColor(&color, optarg)) {
        fprintf(stderr, "Invalid color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    case 'B':
      if (!parseColor(&bg_color, optarg)) {
        fprintf(stderr, "Invalid background color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    default:
      return usage(argv[0]);
    }
  }

  if (bdf_font_file == NULL) {
    fprintf(stderr, "Need to specify BDF font-file with -f\n");
    return usage(argv[0]);
  }

  rgb_matrix::Font font;
  if (!font.LoadFont(bdf_font_file)) {
    fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file);
    return 1;
  }

  RGBMatrix *canvas = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
  if (canvas == NULL){
    return 1;
  }


const bool all_extreme_colors = (matrix_options.brightness == 100)
    && FullSaturation(color)
    && FullSaturation(bg_color);
    
if (all_extreme_colors) {
  canvas->SetPWMBits(1);
}

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    printf("CTRL-C for exit.\n");

    // Create a new canvas to be used with led_matrix_swap_on_vsync
  FrameCanvas *offscreen_canvas = canvas->CreateFrameCanvas();

  const int scroll_direction = (speed >= 0) ? -1 : 1;
  speed = fabs(speed);
  int delay_speed_usec = 1000000;
  if (speed > 0) {
    delay_speed_usec = 1000000 / speed / font.CharacterWidth('W');
  }

  struct timespec next_frame = {0, 0};

  uint64_t frame_counter = 0;

  displayState screens[3] = {
    {"scenes/displayWeek.txt", {}, 0, canvas->width(), 0, 0, true},
    {"scenes/displayEvents.txt", {}, 0, 0, canvas->height(), 0, false},
    {"scenes/displayDailyProgress.txt", {}, 0, 0, canvas->height(), 0, false}
  };

  int curr_screen = 0;

  while (!interrupt_received && loops != 0) {
    displayState &s = screens[curr_screen];
    readLines(s);

    offscreen_canvas->Fill(bg_color.r, bg_color.g, bg_color.b);

    const bool draw_on_frame = (blink_on <= 0)
      || (frame_counter % (blink_on + blink_off) < (uint64_t)blink_on);

    if(draw_on_frame){
      DrawFrame(offscreen_canvas, s, font, color, bg_color, letter_spacing);
    }

    bool next_scene = scroll(s, scroll_direction, canvas->width(), font, x_orig);

    if (next_scene) {
      s.x = s.horizontal_scroll ? canvas->width() : 0;
      s.y = s.horizontal_scroll ? 0 : canvas->height();
      s.max_length = 0;

      // Move to next screen
      curr_screen = (curr_screen + 1) % 3;
      screens[curr_screen].last_change = 0;

      if (loops > 0) --loops;
    }

    if (speed > 0) {
      if (next_frame.tv_sec == 0 && next_frame.tv_nsec == 0) {
        // First time. Start timer, but don't wait.
        clock_gettime(CLOCK_MONOTONIC, &next_frame);
      } else {
        add_micros(&next_frame, delay_speed_usec);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_frame, NULL);
      }
    }
    // Swap the offscreen_canvas with canvas on vsync, avoids flickering
    offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
    if (speed <= 0) pause();  // Nothing to scroll.
    }
}