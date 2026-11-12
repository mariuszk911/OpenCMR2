/*
 * Empty on purpose. third_party/enet/include/enet/unix.h includes <unistd.h>
 * and <sys/time.h> for POSIX calls; neither exists in this toolchain's libnix
 * and the bsdsocket backend (net/enet_bsdsocket.c) uses none of them. This
 * directory is on the include path of the ENet target only, ahead of the
 * system headers, so third_party/enet stays unmodified.
 */
