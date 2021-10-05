/*
 * Copyright © 2012 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "wayland-test-client-utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
create_tmpfile_cloexec (char *tmpname)
{
  int fd;

  fd = mkostemp (tmpname, O_CLOEXEC);
  if (fd >= 0)
    unlink (tmpname);

  return fd;
}

int
create_anonymous_file (off_t size)
{
  static const char template[] = "/wayland-test-client-shared-XXXXXX";
  const char *path;
  char *name;
  int fd;
  int ret;

  path = getenv ("XDG_RUNTIME_DIR");
  if (!path)
    {
      errno = ENOENT;
      return -1;
    }

  name = malloc (strlen (path) + sizeof (template));
  if (!name)
    return -1;

  strcpy (name, path);
  strcat (name, template);

  fd = create_tmpfile_cloexec (name);

  free (name);

  if (fd < 0)
    return -1;

  do
    ret = posix_fallocate (fd, 0, size);
  while (ret == EINTR);

  if (ret != 0)
    {
      close (fd);
      errno = ret;
      return -1;
    }

  return fd;
}
