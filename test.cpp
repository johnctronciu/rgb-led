#include <unistd.h>  // for sleep()

#include "led-matrix.h"
#include "graphics.h"

using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;

int main(int argc, char *argv[]) {
    // Create default options.
    rgb_matrix::RGBMatrix::Options matrix_options;
    matrix_options.rows = 64;
    matrix_options.cols = 64;
    matrix_options.chain_length = 1;
    matrix_options.parallel = 1;
    matrix_options.hardware_mapping = "adafruit-hat";
    matrix_options.brightness=75;

    // Parse runtime flags if desired.
    rgb_matrix::RuntimeOptions runtime_opt;
    
    // Create the matrix
    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
    if (matrix == nullptr) {
        fprintf(stderr, "Could not initialize the matrix\n");
        return 1;;
    }

    Canvas *canvas = matrix;

    // Fill the screen red
    canvas->Fill(255, 0, 0);

    sleep(5); // keep it lit for 5 seconds

    matrix->Clear();
    delete matrix;

    return 0;
}