#define _BSD_SOURCE
#define _XOPEN_SOURCE   600
#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <stdbool.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define PROGRAMMING_PERIOD 2E-3
#define TUTORING_PERIOD 2E-4


struct student_t {
  int priority;
  int time;
  int tutor;
  sem_t being_tutored;
};

// Using Linked list to store the priority of the students
struct node_t {
  int id;
  struct node_t* next;
};

int num_students, num_tutors, num_chairs, num_help;
int empty_chairs, num_waiting, in_session, num_session, num_requests;
struct node_t* waiting_line = NULL;
struct node_t* priority_line = NULL;
struct student_t *students;
pthread_mutex_t warea, tarea, csmc;
sem_t student_coordinator, coordinator_tutor, tutor_coordinator;

int
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec;
}

//Inserting the node based on priorities
int 
higher_priority(int s1, int s2) {
  int diff = students[s1].priority - students[s2].priority;
  if (diff == 0)
    diff = students[s1].time - students[s2].time;
  return diff < 0;
}

void 
show(struct node_t *queue) {
  struct node_t *temp = queue;
  while (temp) {
    printf("%d,", temp->id);
    temp = temp->next;
  }
  printf("\n");
}

void 
priority_push(int id) {
  struct node_t *node = malloc(sizeof(struct node_t));
  node->id = id;
  node->next = NULL;
  if (priority_line) {
    if (higher_priority(id, priority_line->id)) {
      node->next = priority_line;
      priority_line = node;
    } else {
      for (struct node_t *second, *first = priority_line; first; first = first->next) {
        second = first->next;
        if (!second || higher_priority(id, second->id)) {
          node->next = second;
          first->next = node;
          break;
        }
      }
    }
  } else {
    priority_line = node;
  }
}

void 
push(int id) {
  struct node_t *node = malloc(sizeof(struct node_t));
  node->id = id;
  node->next = NULL;
  if (waiting_line) {
    struct node_t *queue = waiting_line;
    while (queue->next)
      queue = queue->next;
    queue->next = node;
  } else {
    waiting_line = node;
  }
}

int 
priority_pop() {
  if (!priority_line)
    return -1;
  int result = priority_line->id;
  struct node_t *temp = priority_line;
  priority_line = priority_line->next;
  free(temp);
  return result;
}

int 
pop() {
  if (!waiting_line)
    return -1;
  int result = waiting_line->id;
  struct node_t *temp = waiting_line;
  waiting_line = waiting_line->next;
  free(temp);
  return result;
}

void*
student(void *id) {
  int sid = *((int*) id);
  int h = num_help;
  srand(263);
  // value beteween 0 to 2000
  int randValue = (rand() % (2000 - 0 + 1)) ;

  
   

  while (h != 0) {
    // programming phase of student
    //sleep(PROGRAMMING_PERIOD);
    usleep(randValue);

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
    push(sid);
    printf("S: Student %d takes a seat. Empty chairs = %d.\n", sid, empty_chairs);
    pthread_mutex_unlock(&warea);

    // notify coordinator using semaphore
    sem_post(&student_coordinator);

    // sleep for tutoring phase
    sem_wait(&students[sid].being_tutored);
    printf("S: Student %d receive help from Tutor %d.\n", sid, students[sid].tutor);

    h--;
  }

  pthread_mutex_lock(&csmc);
  num_students--;
  sem_post(&student_coordinator);
  pthread_mutex_unlock(&csmc);

  return NULL;
}

void*
coordinator(void *args) {
  int at_desk;
  while (1) { 
    pthread_mutex_lock(&csmc);
    if (num_students == 0) {
      pthread_mutex_unlock(&csmc);
      int t;
      for (t= 0 ;t < num_tutors;t++)
        sem_post(&coordinator_tutor);
      break;
    }
    pthread_mutex_unlock(&csmc);

    // wait for student with semaphore
    sem_wait(&student_coordinator);

    // add student to queue
    pthread_mutex_lock(&warea);

    // continue if no students waiting for help
    if (!waiting_line) {
      pthread_mutex_unlock(&warea);
      continue;
    }

    // use a linked list to create a queue -> insert by priority
    for (at_desk = pop(); at_desk != -1; at_desk = pop()) {
      priority_push(at_desk);
      num_requests++;
      num_waiting++;
      printf("C: Student %d with priority %d added to the queue. Waiting students now = %d. Total requests = %d.\n", at_desk, students[at_desk].priority, num_waiting, num_requests);

      // notify the tutor
      sem_post(&coordinator_tutor);
    }

    pthread_mutex_unlock(&warea);

  }

  return NULL;
}

void*
tutor(void *id) {
  int tid = *((int*) id);
  /* int period; */

  while (1) {
    pthread_mutex_lock(&csmc);
    if (num_students == 0) {
      pthread_mutex_unlock(&csmc);
      break;
    }
    pthread_mutex_unlock(&csmc);

    // wait for coordinator
    sem_wait(&coordinator_tutor);

    // find student with highest priority needing help
    int sid;
    pthread_mutex_lock(&warea);
    sid = priority_pop();
    if (sid == -1)
    {
      pthread_mutex_unlock(&warea);
      continue;
    }
    num_waiting--;
    empty_chairs++;
    pthread_mutex_unlock(&warea);

    // start session between student and tutor
    pthread_mutex_lock(&tarea);
    in_session++;
    num_session++;
    pthread_mutex_unlock(&tarea);

    // help the student and release him
    students[sid].tutor = tid;
    /* period = (rand() % (int)(TUTORING_PERIOD * 1000)) * 1000; */
    sleep(TUTORING_PERIOD);
    sem_post(&students[sid].being_tutored);

    pthread_mutex_lock(&tarea);
    printf("T: Student %d tutored by Tutor %d. Students tutored now = %d. Total sessions tutored = %d.\n", sid, tid, in_session, num_session);
    in_session--;
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

  num_students = atoi(argv[1]);
  num_tutors = atoi(argv[2]);
  num_chairs = atoi(argv[3]);
  num_help = atoi(argv[4]);
  
  if(num_tutors <= 0){
     printf("Number of tutors should be greater than 0 \n");
     return -1;
  }
  
  if(num_chairs <= 0){
     printf("Number of chairs should be greater than 0 \n");
     return -1;
  }
  
  empty_chairs = num_chairs;
  assert(pthread_mutex_init(&csmc, NULL) == 0);
  assert(pthread_mutex_init(&warea, NULL) == 0);
  assert(pthread_mutex_init(&tarea, NULL) == 0);

  sem_init(&student_coordinator, 0, 0);
  sem_init(&coordinator_tutor, 0, 0);
  sem_init(&tutor_coordinator, 0, 0);

  // Creating only one coordinator thread because one coordinator is sufficient
  pthread_t coordinator_thread;
  assert(pthread_create(&coordinator_thread, NULL, coordinator, NULL) == 0);

  int *tutor_id = malloc(sizeof(int) * num_tutors);
  //Creating tutor and student threads according to the numbers given in input
  pthread_t *tutor_threads = malloc(sizeof(pthread_t) * num_tutors);
  for(i = 0; i < num_tutors; i++) {
    tutor_id[i] = i;
    assert(pthread_create(&tutor_threads[i], NULL, tutor, (void *) &tutor_id[i]) == 0);
  }

  int *student_id = malloc(sizeof(int) * num_students);
  students = malloc(sizeof(struct student_t) * num_students);
  pthread_t *student_threads = malloc(sizeof(pthread_t) * num_students);
  for(i = 0; i < num_students; i++) {
    student_id[i] = i;
    sem_init(&students[i].being_tutored, 0, 0);
    assert(pthread_create(&student_threads[i], NULL, student, (void *) &student_id[i]) == 0);
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
