#include <stdlib.h>
#include <stdbool.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define PROGRAMMING_PERIOD 0.002
#define TUTORING_PERIOD 0.2

struct student_t {
  int priority;
  int time;
  int tutor;
  sem_t being_tutored;
};

struct node_t {
  int id;
  struct node_t* next;
};

int num_students, num_tutors, num_chairs, num_help, student_at_desk;
int empty_chairs, in_session, num_session, num_requests;
struct node_t* queue = NULL;
struct student_t *students;
pthread_mutex_t warea, tarea, csmc;
sem_t student_need_help, students_waiting_tutors, tutors_waiting_students;

int
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec;
}


int 
higher_priority(int sid1, int sid2) {
  int diff = students[sid1].priority - students[sid2].priority;
  if (diff == 0)
    diff = students[sid1].time - students[sid2].time;
  return diff < 0;
}

void 
push(int id) {
  struct node_t *node = malloc(sizeof(struct node_t));
  node->id = id;
  if (queue) {
    if (higher_priority(id, queue->id)) {
      node->next = queue;
      queue = node;
    } else {
      for (struct node_t *second, *first = queue; first; first = first->next) {
        second = first->next;
        if (!second || higher_priority(id, second->id)) {
          node->next = second;
          first->next = node;
          break;
        }
      }
    }
  } else {
    node->next = queue;
    queue = node;
  }
}

int 
pop() {
  if (!queue)
    return -1;
  int result = queue->id;
  queue = queue->next;
  return result;
}

void*
student(void *id) {
  int sid = *((int*) id);
  int h = num_help;
  int period;
  while (h != 0) {
    // programming phase of student
    period = (rand() % (int)(PROGRAMMING_PERIOD * 1000 + 1)) * 1000;
    sleep(period);

    // visit csmc for help
    pthread_mutex_lock(&warea);
    students[sid].priority++;
    students[sid].time = now();

    // search for an empty chair
    if (empty_chairs == 0) {
      printf("S: Student %d found no empty chair. Will try again later.\n", sid);
      pthread_mutex_unlock(&warea);
      continue;
    }

    // take an empty chair and wait for tutor
    empty_chairs--;
    student_at_desk = sid;
    printf("S: Student %d takes a seat. Empty chairs = %d.\n", sid, empty_chairs);
    pthread_mutex_unlock(&warea);

    // notify coordinator using semaphore
    sem_post(&student_need_help);

    // sleep for tutoring phase
    sem_wait(&students[sid].being_tutored);
    printf("S: Student %d receive help from Tutor %d.\n", sid, students[sid].tutor);

    h--;
  }

  pthread_mutex_lock(&csmc);
  num_students--;
  pthread_mutex_unlock(&csmc);

  return NULL;
}

void*
coordinator(void *args) {

  while (1) {
    pthread_mutex_lock(&csmc);
    if (num_students == 0) {
      pthread_mutex_unlock(&csmc);
      break;
    }
    pthread_mutex_unlock(&csmc);
    // wait for student with semaphore
    sem_wait(&student_need_help);

    // add student to queue
    pthread_mutex_lock(&warea);
    // use a linked list to create a queue -> insert by priority
    push(student_at_desk);
    num_requests++;
    printf("C: Student %d with priority %d added to the queue. Waiting students now = %d. Total requests = %d.\n", student_at_desk, students[student_at_desk].priority, num_chairs - empty_chairs, num_requests);
    pthread_mutex_unlock(&warea);

    // wait for idle tutor 
    sem_wait(&tutors_waiting_students);

    // notify the tutor
    sem_post(&students_waiting_tutors);


  }
  return NULL;
}

void*
tutor(void *id) {
  int tid = *((int*) id);
  int period;

  while (1) {
    pthread_mutex_lock(&csmc);
    if (num_students == 0) {
      pthread_mutex_unlock(&csmc);
      break;
    }
    pthread_mutex_unlock(&csmc);

    // let coordinator know tutor is free
    sem_post(&tutors_waiting_students);

    // wait for coordinator
    sem_wait(&students_waiting_tutors);

    // find student with highest priority needing help
    int sid;
    pthread_mutex_lock(&warea);
    sid = pop();
    empty_chairs++;
    pthread_mutex_unlock(&warea);

    // start session between student and tutor
    pthread_mutex_lock(&tarea);
    in_session++;
    pthread_mutex_unlock(&tarea);

    // help the student and release him
    students[sid].tutor = tid;
    period = (rand() % (int)(TUTORING_PERIOD * 1000 + 1)) * 1000;
    sleep(period);
    sem_post(&students[sid].being_tutored);

    pthread_mutex_lock(&tarea);
    in_session--;
    num_session++;
    printf("Student %d tutored by Tutor %d. Students tutored now = %d. Total sessions tutored = %d.\n", sid, tid, in_session, num_session);
    pthread_mutex_unlock(&tarea);

  }
  return NULL;
}


int 
main(int argc, char* argv[]) {
  if (argc != 5) {
    printf("Need 4 arguments rather than %d\n", argc);
    return -1;
  }

  int i;
  void* value;

  num_students = atoi(argv[0]);
  num_tutors = atoi(argv[1]);
  num_chairs = atoi(argv[2]);
  num_help = atoi(argv[3]);
  
  empty_chairs = num_chairs;
  assert(pthread_mutex_init(&csmc, NULL) == 0);
  assert(pthread_mutex_init(&warea, NULL) == 0);

  sem_init(&student_need_help, 0, 0);
  sem_init(&students_waiting_tutors, 0, 0);
  sem_init(&tutors_waiting_students, 0, 0);

  pthread_t coordinator_thread;
  assert(pthread_create(&coordinator_thread, NULL, coordinator, NULL) == 0);

  pthread_t *tutor_threads = malloc(sizeof(pthread_t) * num_tutors);
  for(i = 0; i < num_tutors; i++) {
    assert(pthread_create(&tutor_threads[i], NULL, tutor, (void *) &i) == 0);
  }

  students = malloc(sizeof(struct student_t) * num_students);
  pthread_t *student_threads = malloc(sizeof(pthread_t) * num_students);
  for(i = 0; i < num_students; i++) {
    sem_init(&students[i].being_tutored, 0, 0);
    assert(pthread_create(&student_threads[i], NULL, student, (void *) &i) == 0);
  }

  assert(pthread_join(coordinator_thread, &value) == 0);
  for(i = 0; i < num_students; i++) {
    assert(pthread_join(student_threads[i], &value) == 0);
  }
  for(i = 0; i < num_tutors; i++) {
    assert(pthread_join(tutor_threads[i], &value) == 0);
  }
  return 0;

}
