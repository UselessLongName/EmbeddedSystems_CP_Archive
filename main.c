#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#define UART0_ADDRESS   ( 0x40004000UL )
#define UART0_DATA      ( * ( ( ( volatile uint32_t * )( UART0_ADDRESS + 0UL ) ) ) )
#define UART0_STATE     ( * ( ( ( volatile uint32_t * )( UART0_ADDRESS + 4UL ) ) ) )
#define UART0_CTRL      ( * ( ( ( volatile uint32_t * )( UART0_ADDRESS + 8UL ) ) ) )
#define UART0_BAUDDIV   ( * ( ( ( volatile uint32_t * )( UART0_ADDRESS + 16UL ) ) ) )
#define TX_BUFFER_MASK  ( 1UL )
#define mainCREATE_SIMPLE_BLINKY_DEMO_ONLY  1
extern void main_blinky( void );
extern void main_full( void );
static void prvUARTInit( void );
static BaseType_t prvUARTReceiveByte( uint8_t *pucByte );
void vFullDemoTickHookFunction( void );
void vFullDemoIdleFunction( void );
static void main_menu_task( void *pvParameters );
static void calculator_task( void *pvParameters );
static void challenge_task( void *pvParameters );
static void timer_task( void *pvParameters );
#define INPUT_BUFFER_SIZE 32
char input_buffer[INPUT_BUFFER_SIZE];
int input_index = 0;
static TaskHandle_t xMainMenuTaskHandle = NULL;
static TaskHandle_t xCalculatorTaskHandle = NULL;
static TaskHandle_t xChallengeTaskHandle = NULL;
static TaskHandle_t xTimerTaskHandle = NULL;
typedef enum { STATE_MAIN_MENU, STATE_CALCULATOR, STATE_CHALLENGE } ProgramState;
static volatile ProgramState program_state = STATE_MAIN_MENU;
typedef enum { CHALLENGE_MENU, CHALLENGE_ACTIVE, CHALLENGE_ENDED } ChallengeState;
static volatile ChallengeState challenge_state = CHALLENGE_MENU;
static volatile int countdown_seconds = 60;
static int correct_answers = 0;
static int total_attempts = 0;
static int current_num1, current_num2, current_answer;
static char current_operator;
#define MAX_PROBLEMS 50
typedef struct {
    int num1, num2;
    char operator;
    int user_answer;
    BaseType_t answered;
    BaseType_t correct;
} Problem_t;
static Problem_t problem_history[MAX_PROBLEMS];
static int problem_count = 0;
static uint32_t random_seed = 1;
static uint32_t get_random( void ) {
    random_seed = (random_seed * 1103515245 + 12345) & 0x7fffffff;
    return random_seed;
}
static int random_range( int min, int max ) {
    return min + (get_random() % (max - min + 1));
}
static void generate_problem( void ) {
    current_num1 = random_range(1, 999);
    current_num2 = random_range(1, 999);
    current_operator = random_range(0, 1) ? '+' : '-';
    if (current_operator == '+') {
        current_answer = current_num1 + current_num2;
    } else {
        current_answer = current_num1 - current_num2;
    }
    if (current_operator == '-' && current_num2 > current_num1) {
        int temp = current_num1;
        current_num1 = current_num2;
        current_num2 = temp;
        current_answer = current_num1 - current_num2;
    }
    if (problem_count < MAX_PROBLEMS) {
        problem_history[problem_count].num1 = current_num1;
        problem_history[problem_count].num2 = current_num2;
        problem_history[problem_count].operator = current_operator;
        problem_history[problem_count].answered = pdFALSE;
        problem_count++;
    }
}
static void trim_spaces(char* str) {
    char* start = str;
    char* end;
    while (*start == ' ') start++;
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    end = str + strlen(str) - 1;
    while (end >= str && *end == ' ') {
        *end = '\0';
        end--;
    }
}
static BaseType_t process_calculator_input(char* input) {
    int num1, num2;
    char operator;
    trim_spaces(input);
    if (strlen(input) == 0) {
        printf("Error: Empty input\r\n");
        return pdFALSE;
    }
    if (strcmp(input, "back") == 0) {
        program_state = STATE_MAIN_MENU;
        return pdTRUE;
    }
    if (sscanf(input, "%d %c %d", &num1, &operator, &num2) == 3) {
        switch (operator) {
            case '+':
                printf("= %d\r\n", num1 + num2);
                break;
            case '-':
                printf("= %d\r\n", num1 - num2);
                break;
            case '*':
                printf("= %d\r\n", num1 * num2);
                break;
            case '/':
                if (num2 != 0) {
                    int quotient = num1 / num2;
                    int remainder = num1 % num2;
                    printf("= %d......%d\r\n", quotient, remainder);
                } else {
                    printf("Error: Division by zero\r\n");
                }
                break;
            default:
                printf("Error: Invalid operator\r\n");
                break;
        }
    } else {
        printf("Error: Invalid input format\r\n");
    }
    return pdFALSE;
}
static BaseType_t process_challenge_input(char* input) {
    int user_answer;
    trim_spaces(input);
    if (strcmp(input, "back") == 0) {
        program_state = STATE_MAIN_MENU;
        countdown_seconds = 0;
        return pdTRUE;
    }
    if (strcmp(input, "begin") == 0) {
        challenge_state = CHALLENGE_MENU;
        countdown_seconds = 0;
        return pdTRUE;
    }
    total_attempts++;
    if (sscanf(input, "%d", &user_answer) == 1) {
        printf("%d=", user_answer);
        if (user_answer == current_answer) {
            correct_answers++;
            printf("Correct!\r\n");
            if (problem_count > 0) {
                problem_history[problem_count - 1].user_answer = user_answer;
                problem_history[problem_count - 1].answered = pdTRUE;
                problem_history[problem_count - 1].correct = pdTRUE;
            }
            generate_problem();
        } else {
            printf("Wrong!\r\n");
            if (problem_count > 0) {
                problem_history[problem_count - 1].user_answer = user_answer;
                problem_history[problem_count - 1].answered = pdTRUE;
                problem_history[problem_count - 1].correct = pdFALSE;
            }
            generate_problem();
        }
    } else {
        printf("Wrong!\r\n");
        if (problem_count > 0) {
            problem_history[problem_count - 1].user_answer = 0;
            problem_history[problem_count - 1].answered = pdTRUE;
            problem_history[problem_count - 1].correct = pdFALSE;
        }
        generate_problem();
    }
    return pdFALSE;
}
static void print_challenge_display( void ) {
    char problem_str[16];
    int cursor_column;
    printf("\033[2J\033[H");
    printf("Challenge begin and end in %ds! Please enter correct answer or enter 'begin'/'back' to start again/go back to main menu\r\n", countdown_seconds);
    for (int i = 0; i < problem_count; i++) {
        if (problem_history[i].answered) {
            printf("%d%c%d=%d %s\r\n",
                   problem_history[i].num1,
                   problem_history[i].operator,
                   problem_history[i].num2,
                   problem_history[i].user_answer,
                   problem_history[i].correct ? "Correct!" : "Wrong!");
        } else {
            printf("%d%c%d=\r\n",
                   problem_history[i].num1,
                   problem_history[i].operator,
                   problem_history[i].num2);
        }
    }
    sprintf(problem_str, "%d%c%d=", problem_history[problem_count - 1].num1,
            problem_history[problem_count - 1].operator,
            problem_history[problem_count - 1].num2);
    cursor_column = strlen(problem_str) + 1;
    printf("\033[%d;%dH", problem_count + 2, cursor_column);
    if (input_index > 0) {
        printf("%s", input_buffer);
    }
}
static void main_menu_task( void *pvParameters ) {
    (void)pvParameters;
    while (1) {
        printf("\033[2J\033[H");
        printf("Main Menu:\r\n");
        printf("1. Calculator\r\n");
        printf("2. Challenge\r\n");
        printf("Enter choice (1-2): ");
        input_index = 0;
        memset(input_buffer, 0, INPUT_BUFFER_SIZE);
        while (1) {
            uint8_t input_char;
            if (prvUARTReceiveByte(&input_char) == pdTRUE) {
                if (input_char == '\r') {
                    input_buffer[input_index] = '\0';
                    trim_spaces(input_buffer);
                    if (strlen(input_buffer) == 0) {
                        printf("\r\nError: Empty input\r\n");
                        break;
                    }
                    if (strcmp(input_buffer, "1") == 0) {
                        program_state = STATE_CALCULATOR;
                        vTaskResume(xCalculatorTaskHandle);
                        vTaskSuspend(NULL);
                        break;
                    } else if (strcmp(input_buffer, "2") == 0) {
                        program_state = STATE_CHALLENGE;
                        vTaskResume(xChallengeTaskHandle);
                        vTaskSuspend(NULL);
                        break;
                    } else {
                        printf("\r\nError: Invalid choice\r\n");
                        break;
                    }
                } else if (input_char == 0x08 || input_char == 0x7F) {
                    if (input_index > 0) {
                        input_index--;
                        input_buffer[input_index] = '\0';
                        printf("\b \b");
                    }
                } else {
                    if (input_index < INPUT_BUFFER_SIZE - 1) {
                        input_buffer[input_index++] = input_char;
                        printf("%c", input_char);
                    } else {
                        printf("\r\nError: Input buffer full\r\n");
                        input_index = 0;
                        memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                        break;
                    }
                }
            }
        }
    }
}
static void calculator_task( void *pvParameters ) {
    (void)pvParameters;
    while (1) {
        printf("\033[2J\033[H");
        printf("Please enter your formula or enter back to go back to main menu: \r\n");
        input_index = 0;
        memset(input_buffer, 0, INPUT_BUFFER_SIZE);
        while (1) {
            uint8_t input_char;
            if (prvUARTReceiveByte(&input_char) == pdTRUE) {
                if (input_char == '\r') {
                    input_buffer[input_index] = '\0';
                    trim_spaces(input_buffer);
                    BaseType_t return_to_menu = process_calculator_input(input_buffer);
                    input_index = 0;
                    memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                    if (return_to_menu) {
                        vTaskResume(xMainMenuTaskHandle);
                        vTaskSuspend(NULL);
                        break;
                    }
                } else if (input_char == 0x08 || input_char == 0x7F) {
                    if (input_index > 0) {
                        input_index--;
                        input_buffer[input_index] = '\0';
                        printf("\b \b");
                    }
                } else {
                    if (input_index < INPUT_BUFFER_SIZE - 1) {
                        input_buffer[input_index++] = input_char;
                        printf("%c", input_char);
                    } else {
                        printf("\r\nError: Input buffer full\r\n");
                        input_index = 0;
                        memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                    }
                }
            }
        }
    }
}
static void timer_task( void *pvParameters ) {
    (void)pvParameters;
    while (1) {
        if (countdown_seconds > 0) {
            printf("\033[s");
            printf("\033[1;1HChallenge begin and end in %ds! Please enter correct answer or enter 'begin'/'back' to start again/go back to main menu", countdown_seconds);
            printf("\033[u");
            countdown_seconds--;
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            vTaskSuspend(NULL);
        }
    }
}
static void challenge_task( void *pvParameters ) {
    (void)pvParameters;
    while (1) {
        challenge_state = CHALLENGE_MENU;
        correct_answers = 0;
        total_attempts = 0;
        countdown_seconds = 0;
        problem_count = 0;
        memset(problem_history, 0, sizeof(problem_history));
        printf("\033[2J\033[H");
        printf("Please enter 'begin'/'back' to start again/go back to main menu: \r\n");
        input_index = 0;
        memset(input_buffer, 0, INPUT_BUFFER_SIZE);
        while (1) {
            if (challenge_state == CHALLENGE_ACTIVE && countdown_seconds == 0) {
                challenge_state = CHALLENGE_ENDED;
                vTaskSuspend(xTimerTaskHandle);
                printf("\033[2J\033[H");
                printf("Challenge ended and you answered %d questions correctly, with a %d%% accuracy rate! Please enter 'begin'/'back' to start again/go back to main menu\r\n",
                       correct_answers, total_attempts > 0 ? (correct_answers * 100 / total_attempts) : 0);
                for (int i = 0; i < problem_count; i++) {
                    if (problem_history[i].answered) {
                        printf("%d%c%d=%d %s\r\n",
                               problem_history[i].num1,
                               problem_history[i].operator,
                               problem_history[i].num2,
                               problem_history[i].user_answer,
                               problem_history[i].correct ? "Correct!" : "Wrong!");
                    } else {
                        printf("%d%c%d=\r\n",
                               problem_history[i].num1,
                               problem_history[i].operator,
                               problem_history[i].num2);
                    }
                }
                input_index = 0;
                memset(input_buffer, 0, INPUT_BUFFER_SIZE);
            }
            uint8_t input_char;
            if (prvUARTReceiveByte(&input_char) == pdTRUE) {
                if (input_char == '\r') {
                    input_buffer[input_index] = '\0';
                    trim_spaces(input_buffer);
                    if (challenge_state == CHALLENGE_MENU) {
                        if (strlen(input_buffer) == 0) {
                            printf("Error: Empty input\r\n");
                            input_index = 0;
                            memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                            continue;
                        }
                        if (strcmp(input_buffer, "back") == 0) {
                            program_state = STATE_MAIN_MENU;
                            vTaskResume(xMainMenuTaskHandle);
                            vTaskSuspend(NULL);
                            break;
                        } else if (strcmp(input_buffer, "begin") == 0) {
                            challenge_state = CHALLENGE_ACTIVE;
                            countdown_seconds = 60;
                            correct_answers = 0;
                            total_attempts = 0;
                            problem_count = 0;
                            memset(problem_history, 0, sizeof(problem_history));
                            input_index = 0;
                            memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                            generate_problem();
                            print_challenge_display();
                            vTaskResume(xTimerTaskHandle);
                        } else {
                            printf("Error: Invalid input\r\n");
                        }
                        input_index = 0;
                        memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                    } else if (challenge_state == CHALLENGE_ACTIVE && countdown_seconds > 0) {
                        BaseType_t action_taken = process_challenge_input(input_buffer);
                        input_index = 0;
                        memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                        if (action_taken) {
                            if (program_state == STATE_MAIN_MENU) {
                                vTaskResume(xMainMenuTaskHandle);
                                vTaskSuspend(NULL);
                                break;
                            } else if (challenge_state == CHALLENGE_MENU) {
                                vTaskSuspend(xTimerTaskHandle);
                                printf("\033[2J\033[H");
                                printf("Please enter 'begin'/'back' to start again/go back to main menu: \r\n");
                            }
                        } else {
                            print_challenge_display();
                        }
                    } else if (challenge_state == CHALLENGE_ENDED) {
                        if (strlen(input_buffer) == 0) {
                            printf("Error: Empty input\r\n");
                            input_index = 0;
                            memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                            continue;
                        }
                        if (strcmp(input_buffer, "back") == 0) {
                            program_state = STATE_MAIN_MENU;
                            vTaskResume(xMainMenuTaskHandle);
                            vTaskSuspend(NULL);
                            break;
                        } else if (strcmp(input_buffer, "begin") == 0) {
                            challenge_state = CHALLENGE_ACTIVE;
                            countdown_seconds = 60;
                            correct_answers = 0;
                            total_attempts = 0;
                            problem_count = 0;
                            memset(problem_history, 0, sizeof(problem_history));
                            input_index = 0;
                            memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                            generate_problem();
                            print_challenge_display();
                            vTaskResume(xTimerTaskHandle);
                        } else {
                            printf("Error: Invalid input\r\n");
                        }
                        input_index = 0;
                        memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                    }
                } else if (input_char == 0x08 || input_char == 0x7F) {
                    if (input_index > 0) {
                        input_index--;
                        input_buffer[input_index] = '\0';
                        printf("\b \b");
                        if (challenge_state == CHALLENGE_ACTIVE) {
                            char problem_str[16];
                            int cursor_column;
                            sprintf(problem_str, "%d%c%d=", problem_history[problem_count - 1].num1,
                                    problem_history[problem_count - 1].operator,
                                    problem_history[problem_count - 1].num2);
                            cursor_column = strlen(problem_str) + 1 + strlen(input_buffer);
                            printf("\033[%d;%dH%s", problem_count + 2, cursor_column - strlen(input_buffer), input_buffer);
                        }
                    }
                } else {
                    if (input_index < INPUT_BUFFER_SIZE - 1) {
                        input_buffer[input_index++] = input_char;
                        printf("%c", input_char);
                    } else {
                        printf("\r\nError: Input buffer full\r\n");
                        input_index = 0;
                        memset(input_buffer, 0, INPUT_BUFFER_SIZE);
                        if (challenge_state == CHALLENGE_ACTIVE) {
                            print_challenge_display();
                        }
                    }
                }
            }
        }
    }
}
void main( void ) {
    prvUARTInit();
    xTaskCreate( main_menu_task, "menu", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 1, &xMainMenuTaskHandle );
    xTaskCreate( calculator_task, "calc", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 1, &xCalculatorTaskHandle );
    xTaskCreate( challenge_task, "challenge", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 1, &xChallengeTaskHandle );
    xTaskCreate( timer_task, "timer", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 2, &xTimerTaskHandle );
    vTaskSuspend(xCalculatorTaskHandle);
    vTaskSuspend(xChallengeTaskHandle);
    vTaskSuspend(xTimerTaskHandle);
    vTaskStartScheduler();
    while(1) {}
}
static void prvUARTInit( void ) {
    UART0_BAUDDIV = 16;
    UART0_CTRL = 3;
}
static BaseType_t prvUARTReceiveByte( uint8_t *pucByte ) {
    if (UART0_STATE & (1UL << 1)) {
        *pucByte = (uint8_t) UART0_DATA;
        return pdTRUE;
    }
    return pdFALSE;
}
int __write( int iFile, char *pcString, int iStringLength ) {
    int iNextChar;
    ( void ) iFile;
    for( iNextChar = 0; iNextChar < iStringLength; iNextChar++ ) {
        while( ( UART0_STATE & TX_BUFFER_MASK ) != 0 );
        UART0_DATA = *pcString;
        pcString++;
    }
    return iStringLength;
}
void vApplicationMallocFailedHook( void ) {
    printf( "\r\n\r\nMalloc failed\r\n" );
    portDISABLE_INTERRUPTS();
    for( ;; );
}
void vApplicationIdleHook( void ) {
}
void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName ) {
    ( void ) pcTaskName;
    ( void ) pxTask;
    printf( "\r\n\r\nStack overflow in %s\r\n", pcTaskName );
    portDISABLE_INTERRUPTS();
    for( ;; );
}
void vApplicationTickHook( void ) {
    #if ( mainCREATE_SIMPLE_BLINKY_DEMO_ONLY != 1 )
    {
        vFullDemoTickHookFunction();
    }
    #endif
}
void vApplicationDaemonTaskStartupHook( void ) {
}
void vAssertCalled( const char *pcFileName, uint32_t ulLine ) {
    volatile uint32_t ulSetToNonZeroInDebuggerToContinue = 0;
    printf( "ASSERT! Line %d, file %s\r\n", ( int ) ulLine, pcFileName );
    taskENTER_CRITICAL();
    {
        while( ulSetToNonZeroInDebuggerToContinue == 0 ) {
            __asm volatile( "NOP" );
            __asm volatile( "NOP" );
        }
    }
    taskEXIT_CRITICAL();
}
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize ) {
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize ) {
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
void *malloc( size_t size ) {
    ( void ) size;
    printf( "\r\n\r\nUnexpected call to malloc() - should be using pvPortMalloc()\r\n" );
    portDISABLE_INTERRUPTS();
    for( ;; );
}
