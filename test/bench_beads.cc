// CPU bench for the BEADS engine paths: renders flat-out (no realtime pacing) and reports
// wall time per block. Compare media 0 (DIGITAL, no conversion) with 1 (VINTAGE, the
// 44.1<->32 kHz converters). Build: see test/run_tests.sh's host build; run with the
// plugin dir as argv[1].
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
extern "C" {
#include "engine.h"   // as engine.cc includes it: C linkage throughout
}



static double now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: bench <plugin dir>\n"); return 2; }
  setenv("ET_PLUGIN_DIR", argv[1], 1);
  const mpc_engine_t* e = mpc_engine();
  const int kBenchSecs = 20;
  for (int media = 0; media <= 1; ++media) {
    void* in = e->create("/tmp/et-bench-data");
    if (!in) { fprintf(stderr, "create failed\n"); return 1; }
    // warm-up: 3 s to fill the buffer past the trim extent
    e->set_param(in, "engine", "2");
    e->set_param(in, "cl_mode", media ? "1" : "0");
    e->set_param(in, "mix", "1");
    e->set_param(in, "grains", "0.7");
    int16_t io[256];
    for (int i = 0; i < 128; ++i) io[2 * i] = io[2 * i + 1] = (int16_t)(6000 * ((i * 37 % 97) / 97.0f - 0.5f));
    for (int b = 0; b < 3 * 44100 / 128; ++b) e->render(in, io, 128);
    double t0 = now();
    int blocks = kBenchSecs * 44100 / 128;
    for (int b = 0; b < blocks; ++b) e->render(in, io, 128);
    double dt = now() - t0;
    printf("media %s: %.1f ms/s of audio (%.2f%% of one core)\n", media ? "VINTAGE  " : "DIGITAL  ",
           dt * 1000.0 / kBenchSecs, 100.0 * dt / kBenchSecs);
    e->destroy(in);
  }
  return 0;
}
