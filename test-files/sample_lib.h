#ifndef SAMPLE_LIB_H
#define SAMPLE_LIB_H

#define MAX_BUFFER_SIZE 2048
#define DEFAULT_SCALE 1.5f
#define LIB_VERSION "1.0.0"

typedef enum {
    STATUS_SUCCESS,
    STATUS_FAILED,
    STATUS_TIMEOUT
} EngineStatus;

typedef struct {
    float x;
    float y;
    float z;
} Vector3;

typedef struct {
    int id;
    Vector3 position;
} Entity;

int engine_init(const char* name, int flags);
Vector3 engine_get_pos(Entity ent);
void engine_shutdown(void);

#endif
