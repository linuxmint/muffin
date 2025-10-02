/*
 * Copyright (C) 2020 Jonas Dreßler.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <core/meta-anonymous-file.h>

#include "wayland-test-client-utils.h"

#define READONLY_SEALS (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)

static const char *teststring = "test string 1234567890";

static int
test_read_fd_mmap (int         fd,
                   const char *expected_string)
{
    void *mem;
    int string_size;

    string_size = strlen (expected_string) + 1;

    mem = mmap (NULL, string_size, PROT_READ, MAP_PRIVATE, fd, 0);
    g_assert (mem != MAP_FAILED);

    if (strcmp (expected_string, mem) != 0)
      {
        munmap (mem, string_size);
        return FALSE;
      }

    munmap (mem, string_size);
    return TRUE;
}

static int
test_write_fd (int         fd,
               const char *string)
{
    int written_size, string_size;

    string_size = strlen (string) + 1;
    written_size = write (fd, string, string_size);
    if (written_size != string_size)
        return FALSE;

    return TRUE;
}

static int
test_readonly_seals (int fd)
{
    unsigned int seals;

    seals = fcntl (fd, F_GET_SEALS);
    if (seals == -1)
        return FALSE;

    if (seals != READONLY_SEALS)
        return FALSE;

    return TRUE;
}

static int
test_write_read (int fd)
{
    g_autofree char *new_string = g_uuid_string_random ();

    if (!test_write_fd (fd, new_string))
        return FALSE;

    if (!test_read_fd_mmap (fd, new_string))
        return FALSE;

    return TRUE;
}

#if defined(HAVE_MEMFD_CREATE)
static int
test_open_write_read (const char *path)
{
    int fd;

    fd = open (path, O_RDWR);
    g_assert (fd != -1);

    if (!test_write_read (fd))
    {
        close (fd);
        return FALSE;
    }

    close (fd);
    return TRUE;
}
#endif

int
main (int    argc,
      char **argv)
{
    MetaAnonymousFile *file;
    int fd = -1, other_fd = -1;
    g_autofree char *fd_path = NULL;

    file = meta_anonymous_file_new (strlen (teststring) + 1,
                                    (const uint8_t *) teststring);
    if (!file)
    {
        g_critical ("%s: Creating file failed", __func__);
        return EXIT_FAILURE;
    }

#if defined(HAVE_MEMFD_CREATE)
  fd = meta_anonymous_file_open_fd (file, META_ANONYMOUS_FILE_MAPMODE_PRIVATE);
  g_assert (fd != -1);
  other_fd = meta_anonymous_file_open_fd (file, META_ANONYMOUS_FILE_MAPMODE_PRIVATE);
  g_assert (other_fd != -1);

  /* When MAPMODE_PRIVATE was used, meta_anonymous_file_open_fd() should always
   * return the same fd. */
  if (other_fd != fd)
    goto fail;

  /* If memfd_create was used and we request a MAPMODE_PRIVATE file, all the
   * readonly seals should be set. */
  if (!test_readonly_seals (fd))
    goto fail;

  if (!test_read_fd_mmap (fd, teststring))
    goto fail;

  /* Writing and reading the written data should fail */
  if (test_write_read (fd))
    goto fail;

  /* Instead we should still be reading the teststring */
  if (!test_read_fd_mmap (fd, teststring))
    goto fail;

  /* Opening the fd manually in RW mode and writing to it should fail */
  fd_path = g_strdup_printf ("/proc/%d/fd/%d", getpid (), fd);
  if (test_open_write_read (fd_path))
    goto fail;

  /* Instead we should still be reading the teststring */
  if (!test_read_fd_mmap (fd, teststring))
    goto fail;

  /* Just to be sure test the other fd, too */
  if (!test_read_fd_mmap (other_fd, teststring))
    goto fail;

  meta_anonymous_file_close_fd (fd);
  meta_anonymous_file_close_fd (fd);


  fd = meta_anonymous_file_open_fd (file, META_ANONYMOUS_FILE_MAPMODE_SHARED);
  g_assert (fd != -1);
  other_fd = meta_anonymous_file_open_fd (file, META_ANONYMOUS_FILE_MAPMODE_SHARED);
  g_assert (other_fd != -1);

  /* The MAPMODE_SHARED fd should not have readonly seals applied */
  if (test_readonly_seals (fd))
    goto fail;

  if (!test_read_fd_mmap (fd, teststring))
    goto fail;

  if (!test_read_fd_mmap (other_fd, teststring))
    goto fail;

  /* Writing and reading the written data should succeed */
  if (!test_write_read (fd))
    goto fail;

  /* The other fd should still read the teststring though */
  if (!test_read_fd_mmap (other_fd, teststring))
    goto fail;

  meta_anonymous_file_close_fd (fd);
  meta_anonymous_file_close_fd (other_fd);


  /* Test an artificial out-of-space situation by setting the maximium file
   * size this process may create to 2 bytes, if memfd_create with
   * MAPMODE_PRIVATE is used, everything should still work (the existing FD
   * should be used). */
  struct rlimit limit = {2, 2};
  if (setrlimit (RLIMIT_FSIZE, &limit) == -1)
    goto fail;

  fd = meta_anonymous_file_open_fd (file, META_ANONYMOUS_FILE_MAPMODE_PRIVATE);
  g_assert (fd != -1);

  if (!test_read_fd_mmap (fd, teststring))
    goto fail;

  meta_anonymous_file_close_fd (fd);
#else
  fd = meta_anonymous_file_open_fd (file, META_ANONYMOUS_FILE_MAPMODE_PRIVATE);
  g_assert (fd != -1);
  other_fd = meta_anonymous_file_open_fd (file, META_ANONYMOUS_FILE_MAPMODE_PRIVATE);
  g_assert (other_fd != -1);

  if (test_readonly_seals (fd))
    goto fail;

  /* Writing and reading the written data should succeed */
  if (!test_write_read (fd))
    goto fail;

  /* The other fd should still read the teststring though */
  if (!test_read_fd_mmap (other_fd, teststring))
    goto fail;

  meta_anonymous_file_close_fd (fd);
  meta_anonymous_file_close_fd (other_fd);


  fd = meta_anonymous_file_open_fd (file, META_ANONYMOUS_FILE_MAPMODE_SHARED);
  g_assert (fd != -1);
  other_fd = meta_anonymous_file_open_fd (file, META_ANONYMOUS_FILE_MAPMODE_SHARED);
  g_assert (other_fd != -1);

  if (test_readonly_seals (fd))
    goto fail;

  if (!test_read_fd_mmap (fd, teststring))
    goto fail;

  if (!test_read_fd_mmap (other_fd, teststring))
    goto fail;

  /* Writing and reading the written data should succeed */
  if (!test_write_read (fd))
    goto fail;

  /* The other fd should still read the teststring though */
  if (!test_read_fd_mmap (other_fd, teststring))
    goto fail;

  meta_anonymous_file_close_fd (fd);
  meta_anonymous_file_close_fd (other_fd);
#endif

  meta_anonymous_file_free (file);
  return EXIT_SUCCESS;

  fail:
  if (fd > 0)
    meta_anonymous_file_close_fd (fd);
  if (other_fd > 0)
    meta_anonymous_file_close_fd (other_fd);
  meta_anonymous_file_free (file);
  return EXIT_FAILURE;
}
