#ifndef SAFEFILE_H
#define SAFEFILE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A small file that a full drive or a power cut can't lose: the new one is
 * written and flushed beside it (name + ".NEW"), and only then takes its place.
 * False if the old one is still what's there. A cut between the two steps leaves
 * only the .NEW: safefile_read(path, true, ...) reads that, for when the file
 * itself is missing or doesn't load. */
bool safefile_save(const char *path, const void *data, size_t size);

/* For a file someone else writes, to name + ".NEW": that takes the file's place.
 * False if the old one is still what's there. */
bool safefile_take_new(const char *path);

/* After a cut in safefile_take_new(): the file is missing and its .NEW is
 * whole, so it becomes the file. Only for writers that make .NEW all at once. */
void safefile_recover(const char *path);

/* The file and its .NEW. False if the drive couldn't be written. */
bool safefile_remove(const char *path);

/* Its bytes, 0 if it can't be read. */
size_t safefile_read(const char *path, bool new_copy, void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
