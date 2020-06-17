#include <stdio.h>
#include <sqlite3.h>
#include <stdlib.h>
#include "include/Base.h"
#include "include/Agent.h"

void base_init(struct Base *base, struct World *world) {
  base->world = world;
}

static int check_sql(int rc, char *err_msg) {
  if (rc != SQLITE_OK) {
     fprintf(stderr, "Got error code %i\n", rc);
     fprintf(stderr, "SQL error: %s\n", err_msg);
     exit(-1);
  }
  return 1;
}

void base_saveworld(struct Base *base) {
  printf("Saving world...\n");

  sqlite3 *db;
  sqlite3_stmt *res;
  char *err_msg = 0;
  int rc = sqlite3_open("world.db", &db);

  if (rc != SQLITE_OK) {
    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  char *agent_sql = "DROP TABLE IF EXISTS Agents;"
                    "CREATE TABLE Agents(agent BLOB);";
  check_sql(sqlite3_exec(db, agent_sql, 0, 0, &err_msg), err_msg);

  size_t nagents = base->world->agents.size;
  printf("Found %li agents\n", nagents);

  for (size_t i = 0; i < nagents; i++) {
    struct Agent *a = avec_get(&base->world->agents, i);
    char *a_insert = "INSERT INTO Agents (agent) VALUES (?);";
    check_sql(sqlite3_prepare_v2(db, a_insert, -1, &res, 0), err_msg);
    check_sql(sqlite3_bind_blob(res, 1, a, sizeof(struct Agent), SQLITE_TRANSIENT), err_msg);
    sqlite3_step(res);
  }


  sqlite3_finalize(res);
  sqlite3_close(db);

  printf("Done!\n");
}

void base_loadworld(struct Base *base) {
  printf("Fake world load!\n");
}
