/*
 *  depends.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE

#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "aur.h"
#include "conf.h"
#include "depends.h"
#include "download.h"
#include "search.h"
#include "util.h"

static alpm_list_t *parse_bash_array(alpm_list_t *deplist, char *deparray, int stripdeps) {
  char *token;

  token = strtok(deparray, " \n");
  while (token) {
    if (*token == '\'' || *token == '\"')
      token++;

    if (stripdeps)
      *(token + strcspn(token, "=<>\"\'")) = '\0';
    else {
      char *ptr = token + strlen(token) - 1;
      if (*ptr == '\'' || *ptr == '\"' )
        *ptr = '\0';
    }

    if (config->verbose >= 2)
      fprintf(stderr, "::DEBUG Adding Depend: %s\n", token);

    if (alpm_list_find_str(deplist, token) == NULL)
      deplist = alpm_list_add(deplist, strdup(token));

    token = strtok(NULL, " \n");
  }

  return deplist;
}

alpm_list_t *pkgbuild_get_deps(char *pkgbuild) {
  char *bptr, *lineptr, *arrayend;
  alpm_list_t *deplist;

  deplist = NULL;
  bptr = pkgbuild;

  /* Read line by line even though we've got the entire file in a buffer */
  while (bptr && (lineptr = strchr(bptr, '\n'))) {
    strtrim(++lineptr);

    if ((line_starts_with(lineptr, PKGBUILD_DEPENDS) != 0) && 
        (line_starts_with(lineptr, PKGBUILD_MAKEDEPENDS) != 0)) {

      bptr = strchr(lineptr, '\n');
      continue;
    }

    arrayend = strchr(lineptr, ')');
    *arrayend  = '\0';

    deplist = parse_bash_array(deplist, strchr(lineptr, '(') + 1, TRUE);
    bptr = arrayend + 1;
  }

  return deplist;
}

struct aur_pkg_t *populate_pkg_deps(struct aur_pkg_t *pkg, char *pkgbuild) {
  char *bptr, *lineptr, *arrayend;

  bptr = pkgbuild;

  /* Read line by line even though we've got the entire file in a buffer */
  while (bptr && (lineptr = strchr(bptr, '\n'))) {
    strtrim(++lineptr);

    alpm_list_t **deplist;
    if (line_starts_with(lineptr, PKGBUILD_DEPENDS) == 0 ) {
      deplist = &(pkg->depends);
    } else if (line_starts_with(lineptr, PKGBUILD_MAKEDEPENDS) == 0) {
      deplist = &(pkg->makedepends);
    } else if (line_starts_with(lineptr, PKGBUILD_OPTDEPENDS) == 0) {
      deplist = &(pkg->optdepends);
    } else {
      bptr = strchr(lineptr, '\n');
      continue;
    }

    arrayend = strchr(lineptr, ')');
    *arrayend  = '\0';

    *deplist = parse_bash_array(*deplist, strchr(lineptr, '(') + 1, FALSE);
    bptr = arrayend + 1;
  }

  return pkg;
}

int get_pkg_dependencies(const char *pkg) {
  const char *dir;
  char *pkgbuild_path, *buffer;
  int ret = 0;

  if (config->download_dir == NULL)
    dir = getcwd(NULL, 0);
  else
    dir = realpath(config->download_dir, NULL);

  pkgbuild_path = calloc(1, PATH_MAX + 1);
  snprintf(pkgbuild_path, strlen(dir) + strlen(pkg) + 11, "%s/%s/PKGBUILD", dir, pkg);

  buffer = get_file_as_buffer(pkgbuild_path);
  if (! buffer) {
    if (config->color)
      cfprintf(stderr, "%<::%> Could not open PKGBUILD for dependency parsing.\n",
        config->colors->error);
    else
      fprintf(stderr, "!! Could not open PKGBUILD for dependency parsing.\n");
    return 1;
  }

  alpm_list_t *deplist = pkgbuild_get_deps(buffer);
  free(buffer);

  if (!config->quiet && config->verbose >= 1) {
    if (config->color)
      cprintf("\n%<::%> Fetching uninstalled dependencies for %<%s%>...\n",
        config->colors->info, config->colors->pkg, pkg);
    else
      printf("\n:: Fetching uninstalled dependencies for %s...\n", pkg);
  }

  alpm_list_t *i = deplist;
  for (i = deplist; i; i = i->next) {
    const char* depend = i->data;
    if (config->verbose >= 2)
      printf("::DEBUG Attempting to find %s\n", depend);

    if (alpm_db_get_pkg(db_local, depend)) { /* installed */
      if (config->verbose >= 2)
        printf("::DEBUG %s is installed\n", depend);

      continue;
    }

    if (is_in_pacman(depend)) /* available in pacman */
      continue;

    /* if we're here, we need to check the AUR */
    alpm_list_t *results = aur_rpc_query(AUR_QUERY_TYPE_INFO, depend);
    if (results) {

      if (config->verbose >= 2)
        printf("::DEBUG %s is in the AUR\n", depend);

      ret++;
      aur_get_tarball(results->data);

      aur_pkg_free(results->data);
      alpm_list_free(results);
    }
    /* Silently ignore packages that can't be found anywhere */
  }

  /* Cleanup */
  FREELIST(deplist);
  FREE(dir);
  FREE(pkgbuild_path);

  return ret;
}

/* vim: set ts=2 sw=2 et: */
