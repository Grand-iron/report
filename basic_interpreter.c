/* Basic Interpreter by H?eyin Uslu raistlinthewiz@hotmail.com */
/* Code licenced under GPL */

#include <stdio.h>
#include <conio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef _WIN32
#define CLEAR() system("cls")
#else
#define CLEAR() system("clear")
#endif

/*
 * node
 *  - 기본 심볼/구문 트리 노드로 사용되는 연결 리스트 요소입니다.
 *  - type: 노드 종류를 나타냅니다 (1: 변수 선언, 2: 함수 선언, 3: 함수 호출, 4: begin, 5: end).
 *  - exp_data: 변수명/심볼을 단일 문자로 저장합니다 (예: 'a', 'b').
 *  - val: 변수의 정수 값 또는 노드와 연관된 값입니다.
 *  - line: 함수 선언일 경우 해당 함수의 시작 라인(코드 위치)을 저장합니다.
 *  - next: 스택/심볼 테이블로 연결하기 위한 포인터입니다.
 */
struct node {
    int type; /* 1 var, 2 function, 3 function call, 4 begin, 5 end */
    char exp_data;
    int val;
    int line;
    struct node* next;
};
typedef struct node Node;

/*
 * stack
 *  - Node 포인터의 스택 래퍼입니다. 프로그램에서 변수/함수/제어블록을
 *    스택(연결 리스트) 형태로 관리할 때 사용합니다.
 */
struct stack { Node* top; };
typedef struct stack Stack;

/*
 * opnode / opstack
 *  - 수학식에서 연산자들을 임시로 저장하는 스택 노드와 스택 구조체입니다.
 *  - 변환(infix -> postfix) 과정에서 연산자 우선순위를 관리하는 용도입니다.
 */
struct opnode { char op; struct opnode* next; };
typedef struct opnode opNode;

struct opstack { opNode* top; };
typedef struct opstack OpStack;

/*
 * postfixnode / postfixstack
 *  - 후위 표기(postfix) 계산을 위한 스택 노드와 스택 구조체입니다.
 *  - 숫자값들을 푸시/팝 하여 실제 연산을 수행합니다.
 */
struct postfixnode { int val; struct postfixnode* next; };
typedef struct postfixnode Postfixnode;

struct postfixstack { Postfixnode* top; };
typedef struct postfixstack PostfixStack;

/*
 * 파일-스코프 정적 함수들 (이 소스 파일 내부에서만 사용)
 *  - GetVal: 스택(심볼 테이블)에서 이름(단일 문자)에 해당하는 값(또는 함수 시작 라인)을 검색합니다.
 *            반환값: 변수이면 값, 함수이면 -1(함수의 라인은 출력 인자로 설정), 없으면 -999.
 *  - GetLastFunctionCall: 스택을 뒤로 훑어 가장 최근에 푸시된 "함수 호출" 노드(type==3)의 라인 번호를 반환합니다.
 *  - FreeAll: 스택에 남아있는 모든 Node를 해제하고 메모리를 정리합니다.
 *  - my_stricmp: 대소문자 구분 없는 문자열 비교 (case-insensitive strcmp).
 *  - rstrip: 문자열 오른쪽의 개행/캐리지리턴/공백을 제거합니다.
 */
static int GetVal(char, int*, Stack*);
static int GetLastFunctionCall(Stack*);
static Stack* FreeAll(Stack*);
static int my_stricmp(const char* a, const char* b);
static void rstrip(char* s);

/*
 * Push
 *  - 스택(`Stack`)에 새로운 `Node` 값을 푸시한다.
 *  - 입력: sNode (복사할 노드 값), stck (대상 스택 포인터)
 *  - 출력: 변경된 스택 포인터 (같은 stck를 반환) 또는 메모리 할당 실패 시 NULL
 *  - 부수효과: 힙에 새 노드를 할당하고 stck->top을 갱신함.
 */
static Stack* Push(Node sNode, Stack* stck)
{
    Node* newnode = (Node*)malloc(sizeof(Node));
    if (!newnode) { printf("ERROR, Couldn't allocate memory..."); return NULL; }
    newnode->type = sNode.type;
    newnode->val = sNode.val;
    newnode->exp_data = sNode.exp_data;
    newnode->line = sNode.line;
    newnode->next = stck->top;
    stck->top = newnode;
    return stck;
}

/*
 * PushOp
 *  - 연산자(`char op`)를 연산자 스택(`OpStack`)에 푸시한다.
 *  - 입력: op (연산자 기호), opstck (대상 연산자 스택)
 *  - 출력: 갱신된 연산자 스택 포인터 또는 할당 실패 시 NULL
 *  - 부수효과: 새 opNode를 힙에 할당하고 opstck->top을 갱신함.
 */
static OpStack* PushOp(char op, OpStack* opstck)
{
    opNode* newnode = (opNode*)malloc(sizeof(opNode));
    if (!newnode) { printf("ERROR, Couldn't allocate memory..."); return NULL; }
    newnode->op = op;
    newnode->next = opstck->top;
    opstck->top = newnode;
    return opstck;
}

/*
 * PopOp
 *  - 연산자 스택에서 최상단 연산자를 팝하고 그 값을 반환한다.
 *  - 입력: opstck (연산자 스택)
 *  - 출력: 팝된 연산자 문자 또는 스택이 비어있으면 0
 *  - 부수효과: 최상단 opNode를 해제하고 opstck->top을 갱신함.
 */
static char PopOp(OpStack* opstck)
{
    opNode* temp;
    char op;
    if (opstck->top == NULL)
    {
        return 0;
    }
    op = opstck->top->op;
    temp = opstck->top;
    opstck->top = opstck->top->next;
    free(temp);
    return op;
}

/*
 * PushPostfix
 *  - 후위 계산용 스택에 정수 값을 푸시한다.
 *  - 입력: val (정수 값), poststck (후위 스택)
 *  - 출력: 갱신된 후위 스택 포인터 또는 할당 실패 시 NULL
 *  - 부수효과: 새 Postfixnode를 힙에 할당하고 poststck->top을 갱신함.
 */
static PostfixStack* PushPostfix(int val, PostfixStack* poststck)
{
    Postfixnode* newnode = (Postfixnode*)malloc(sizeof(Postfixnode));
    if (!newnode) { printf("ERROR, Couldn't allocate memory..."); return NULL; }
    newnode->val = val;
    newnode->next = poststck->top;
    poststck->top = newnode;
    return poststck;
}

/*
 * PopPostfix
 *  - 후위 계산용 스택에서 최상단 정수 값을 팝하여 반환한다.
 *  - 입력: poststck (후위 스택)
 *  - 출력: 팝된 정수 값 또는 스택이 비어있으면 0
 *  - 부수효과: 최상단 Postfixnode를 해제하고 poststck->top을 갱신함.
 */
static int PopPostfix(PostfixStack* poststck)
{
    Postfixnode* temp;
    int val;
    if (poststck->top == NULL)
    {
        return 0;
    }
    val = poststck->top->val;
    temp = poststck->top;
    poststck->top = poststck->top->next;
    free(temp);
    return val;
}

/*
 * Pop
 *  - 일반 Node 스택에서 최상단 노드를 팝해 호출자에게 복사한다.
 *  - 입력: sNode (출력용 버퍼 포인터), stck (대상 스택)
 *  - 출력: sNode에 팝된 노드의 필드가 복사됨. 스택이 비어있으면 아무 동작도 하지 않음.
 *  - 부수효과: 최상단 Node를 해제하고 stck->top을 갱신함.
 */
static void Pop(Node* sNode, Stack* stck)
{
    Node* temp;
    if (stck->top == NULL) return;
    sNode->exp_data = stck->top->exp_data;
    sNode->type = stck->top->type;
    sNode->line = stck->top->line;
    sNode->val = stck->top->val;
    temp = stck->top;
    stck->top = stck->top->next;
    free(temp);
}

/*
 * isStackEmpty
 *  - 연산자 스택이 비어있는지 검사한다.
 *  - 입력: stck (연산자 스택)
 *  - 출력: 비어있으면 1, 아니면 0
 */
static int isStackEmpty(OpStack* stck)
{
    return stck->top == 0;
}

/*
 * Priotry (오타: Priority)
 *  - 간단한 연산자 우선순위 판정 함수.
 *  - 입력: operator ('+', '-', '*', '/')
 *  - 출력: 우선순위 정수 (+:1, -:1, *:2, /:2, 그 외:0)
 */
static int Priotry(char operator)
{
    if ((operator=='+') || (operator=='-')) return 1;
    else if ((operator=='/') || (operator=='*')) return 2;
    return 0;
}

/*
 * main
 *  - SPL 스크립트 파일을 읽어들이고, 각 라인을 파싱해
 *    함수 정의 / 변수 선언 / 블록(begin~end) / 식 계산 / 함수 호출을
 *    스택 기반으로 처리하여 최종 결과를 출력하는 인터프리터의 진입점.
 *
 *  주요 역할(전체 흐름) 및 구현 위치(라인 번호):
 *
 *   1) 프로그램 시작:
 *      - main 진입부 선언:                             (라인: 279)
 *      - 화면 초기화(CLEAR) 호출:                     (라인: 315)
 *      - 명령행 인자 검사(argc != 2) 및 에러 처리:     (라인: 317-325)
 *      - SPL 소스 파일 열기(fopen):                    (라인: 326)
 *      - 한 줄씩 읽기 위한 메인 루프 시작(fgets):      (라인: 327)
 *
 *   2) 라인 단위 파싱:
 *      - 읽은 라인을 토큰화하여 키워드를 판별하고 다음을 수행:
 *
 *        (1) 함수 선언(function):
 *            - 함수 처리 시작(토큰 비교):              (라인: 418)
 *            - 함수 선언을 스택에 기록(함수명, 시작 라인): (라인: 423-427)
 *
 *        (2) 변수 선언(int):
 *            - 변수 선언 처리(토큰 비교):               (라인: 395)
 *            - 변수 값을 스택에 저장:                  (라인: 413-415)
 *
 *        (3) 블록(begin/end):
 *            - begin 처리(스택에 begin 표시):          (라인: 346-351)
 *            - end 처리(스택에 end 표시 및 반환 검사): (라인: 354-363)
 *            - 함수 호출이 있는 경우 파일 재위치 및 복귀 처리: (라인: 372-386)
 *
 *        (4) 식 계산((...)):
 *            - 식 토큰 판별(괄호 시작):                (라인: 447)
 *            - 중위->후위 변환 루프 시작:               (라인: 456)
 *            - 연산자 우선순위 판정/스택 처리:          (라인: 471-489)
 *            - 식 내 식별자 처리(변수/함수 구분):       (라인: 491-511)
 *            - 함수 호출 시 콜 프레임 푸시:             (라인: 507-510)
 *            - 호출 대상 함수로 파일 포인터 이동(fclose/fopen): (라인: 513-515)
 *            - 후위식 계산 루프 시작:                   (라인: 552)
 *            - 계산 결과 저장(LastExpReturn):          (라인: 575)
 *
 *   3) 함수 호출 처리:
 *      - 함수 호출 시 콜 프레임(push) 및 라인 이동:    (라인: 507-515)
 *      - 함수 내 end에서 부모로 반환값 전달 처리:      (라인: 358-366, 371)
 *
 *   4) 프로그램 종료:
 *      - 파일 닫기(fclose) 및 스택 정리(FreeAll):      (라인: 583-584)
 *      - 프로그램 종료(return 0):                     (라인: 588)
 *
 *  입력:
 *    - 명령행 인자: SPL 소스 파일 경로
 *
 *  출력:
 *    - SPL 프로그램을 실행한 결과를 표준 출력에 표시함
 *    - 내부적으로 여러 스택을 사용하여 상태를 관리하고
 *      함수 호출 시 파일 포인터를 이동시켜 실행 흐름을 제어함
 */

int main(int argc, char** argv)
{
    char line[4096];                /* 입력 파일에서 한 줄을 읽어오는 버퍼 */
    char dummy[4096];               /* 파일 탐색/임시 읽기용 버퍼 (함수 호출 시 라인 스킵용) */
    char lineyedek[4096];           /* 원본 라인의 복사본: 수식 파싱 시 읽기용 */
    char postfix[4096];             /* 중위->후위 변환 후의 후위 표기 문자열 저장 버퍼 */
    char* firstword;                /* strtok로 분리한 첫 번째 토큰 포인터 */

    int val1;                       /* 후위 계산에서 피연산자1(팝한 값) */
    int val2;                       /* 후위 계산에서 피연산자2(팝한 값) */

    int LastExpReturn = 0;          /* 마지막으로 계산된 식의 결과값 */
    int LastFunctionReturn = -999;  /* 함수 반환값 전달용(-999이면 없음) */
    int CalingFunctionArgVal = 0;   /* 현재 호출 중인 함수로 전달할 인자값 */

    Node tempNode;                  /* 스택에서 팝/푸시 시 사용되는 임시 노드 버퍼 */

    OpStack* MathStack = (OpStack*)malloc(sizeof(OpStack));      /* 연산자 스택 (중위->후위 변환용) */
    FILE* filePtr;                  /* 입력 SPL 파일 포인터 */
    PostfixStack* CalcStack = (PostfixStack*)malloc(sizeof(PostfixStack)); /* 후위 계산용 스택 */
    int resultVal = 0;              /* 개별 연산 결과 임시 저장 */
    Stack* STACK = (Stack*)malloc(sizeof(Stack));               /* 심볼/실행 스택 (변수, 함수, 블록 표시) */

    int curLine = 0;                /* 현재 읽고 있는 파일의 라인 번호 (1-based) */
    int foundMain = 0;              /* main 함수(실행 시작 함수)를 찾았는지 표시 */
    int WillBreak = 0;              /* 함수 호출로 파일 포인터를 이동할 때 루프 중단 플래그 */

    if (!MathStack || !CalcStack || !STACK) {
        printf("Memory alloc failed\n");
        return 1;
    }
    MathStack->top = NULL;
    CalcStack->top = NULL;
    STACK->top = NULL;

    /* SECTION: 화면 초기화 — 화면을 지우고 실행 환경을 초기화함 */
    CLEAR();

    /* SECTION: 인자 검사 — 프로그램은 하나의 소스 파일 경로를 필요로 함 */
    if (argc != 2)
    {
        printf("Incorrect arguments!\n");
        printf("Usage: %s <inputfile.spl>", argv[0]);
        return 1;
    }

    /* SECTION: 파일 열기 — 명시된 SPL 소스 파일을 연다 */
    filePtr = fopen(argv[1], "r");
    if (filePtr == NULL)
    {
        printf("Can't open %s. Check the file please", argv[1]);
        return 2;
    }

    /* SECTION: 메인 루프 — 파일에서 한 줄씩 읽어 처리함 */
    while (fgets(line, 4096, filePtr))
    {
        int k = 0;

        while (line[k] != '\0')
        {
            if (line[k] == '\t') line[k] = ' ';
            k++;
        }

        rstrip(line);
        strcpy(lineyedek, line);

        curLine++;
        tempNode.val = -999;
        tempNode.exp_data = ' ';
        tempNode.line = -999;
        tempNode.type = -999;

    /* SECTION: begin 처리 — 블록 시작을 스택에 표시 */
    if (my_stricmp("begin", line) == 0)
        {
            if (foundMain)
            {
                tempNode.type = 4;
                STACK = Push(tempNode, STACK);
            }
        }
    /* SECTION: end 처리 — 블록/함수 종료, 반환값 출력 또는 호출 복귀 처리 */
    else if (my_stricmp("end", line) == 0)
        {
            if (foundMain)
            {
                int sline;
                tempNode.type = 5;
                STACK = Push(tempNode, STACK);

                sline = GetLastFunctionCall(STACK);
                if (sline == 0)
                {
                    printf("Output=%d", LastExpReturn);
                }
                else
                {
                    int j;
                    int foundCall = 0;
                    LastFunctionReturn = LastExpReturn;

                    fclose(filePtr);
                    filePtr = fopen(argv[1], "r");
                    curLine = 0;
                    for (j = 1; j < sline; j++)
                    {
                        fgets(dummy, 4096, filePtr);
                        curLine++;
                    }

                    while (foundCall == 0)
                    {
                        Pop(&tempNode, STACK);
                        if (tempNode.type == 3) foundCall = 1;
                    }
                }
            }
        }
        else
        {
            firstword = strtok(line, " ");
            if (!firstword) continue;

            /* SECTION: 변수 선언 처리 — 'int <var> [= value]' */
            if (my_stricmp("int", firstword) == 0)
            {
                if (foundMain)
                {
                    tempNode.type = 1;
                    firstword = strtok(NULL, " ");
                    if (!firstword) continue;
                    tempNode.exp_data = firstword[0];

                    firstword = strtok(NULL, " ");
                    if (!firstword) continue;

                    if (my_stricmp("=", firstword) == 0)
                    {
                        firstword = strtok(NULL, " ");
                        if (!firstword) continue;
                    }

                    tempNode.val = atoi(firstword);
                    tempNode.line = 0;
                    STACK = Push(tempNode, STACK);
                }
            }
            /* SECTION: 함수 선언 처리 — 'function <name> [arg]' */
            else if (my_stricmp("function", firstword) == 0)
            {
                firstword = strtok(NULL, " ");
                if (!firstword) continue;

                tempNode.type = 2;
                tempNode.exp_data = firstword[0];
                tempNode.line = curLine;
                tempNode.val = 0;
                STACK = Push(tempNode, STACK);

                if (firstword[0] == 'm' && firstword[1] == 'a' && firstword[2] == 'i' && firstword[3] == 'n')
                {
                    foundMain = 1;
                }
                else
                {
                    if (foundMain)
                    {
                        firstword = strtok(NULL, " ");
                        if (!firstword) continue;
                        tempNode.type = 1;
                        tempNode.exp_data = firstword[0];
                        tempNode.val = CalingFunctionArgVal;
                        tempNode.line = 0;
                        STACK = Push(tempNode, STACK);
                    }
                }
            }
            /* SECTION: 식 처리(중위->후위->계산) — '('로 시작하는 표현식 처리 */
            else if (firstword[0] == '(')
            {
                if (foundMain)
                {
                    int i = 0;
                    int y = 0;

                    MathStack->top = NULL;

                    /* SECTION: 중위->후위 변환 루프 시작 — lineyedek 문자 하나씩 분석 */
                    while (lineyedek[i] != '\0')
                    {
                        if (isdigit((unsigned char)lineyedek[i]))
                        {
                            postfix[y] = lineyedek[i];
                            y++;
                        }
                        else if (lineyedek[i] == ')')
                        {
                            if (!isStackEmpty(MathStack))
                            {
                                postfix[y] = PopOp(MathStack);
                                y++;
                            }
                        }
                        /* SECTION: 연산자 처리 — 우선순위에 따라 연산자 스택에 푸시/팝 */
                        else if (lineyedek[i] == '+' || lineyedek[i] == '-' || lineyedek[i] == '*' || lineyedek[i] == '/')
                        {
                            if (isStackEmpty(MathStack))
                            {
                                MathStack = PushOp(lineyedek[i], MathStack);
                            }
                            else
                            {
                                if (Priotry(lineyedek[i]) <= Priotry(MathStack->top->op))
                                {
                                    postfix[y] = PopOp(MathStack);
                                    y++;
                                    MathStack = PushOp(lineyedek[i], MathStack);
                                }
                                else
                                {
                                    MathStack = PushOp(lineyedek[i], MathStack);
                                }
                            }
                        }
                        /* SECTION: 식별자 처리 — 변수인지 함수(호출)인지 판별 */
                        else if (isalpha((unsigned char)lineyedek[i]) > 0)
                        {
                            int codeline = 0;
                            int dummyint = 0;
                            int retVal = GetVal(lineyedek[i], &codeline, STACK);

                            if ((retVal != -1) && (retVal != -999))
                            {
                                postfix[y] = (char)(retVal + 48);
                                y++;
                            }
                            else
                            {
                                if (LastFunctionReturn == -999)
                                {
                                    /* SECTION: 함수 호출 감지 및 콜프레임 푸시 — 호출을 위해 현재 상태를 스택에 저장 */
                                    int j;
                                    tempNode.type = 3;
                                    tempNode.line = curLine;
                                    STACK = Push(tempNode, STACK);

                                    /* SECTION: 호출 인자 값 확인 — 호출부의 인자(예: a(x))에서 x값 추출 */
                                    CalingFunctionArgVal = GetVal(lineyedek[i + 2], &dummyint, STACK);

                                    /* SECTION: 호출 대상 함수로 파일 포인터 이동 — 파일을 처음부터 다시 열어 해당 함수 시작 라인으로 이동 */
                                    fclose(filePtr);
                                    filePtr = fopen(argv[1], "r");
                                    curLine = 0;

                                    for (j = 1; j < codeline; j++)
                                    {
                                        fgets(dummy, 4096, filePtr);
                                        curLine++;
                                    }

                                    WillBreak = 1; /* 메인 루프 일시 중단 표시 */
                                    break;
                                }
                                else
                                {
                                    /* SECTION: 함수 반환값 사용 — 이미 반환된 값이 있으면 식에 삽입 */
                                    postfix[y] = (char)(LastFunctionReturn + 48);
                                    y++;
                                    i = i + 3;
                                    LastFunctionReturn = -999;
                                }
                            }
                        }
                        i++;
                    }

                    /* SECTION: 후위식 완성 및 계산 — WillBreak가 설정되지 않았을 때만 계산 수행 */
                    if (WillBreak == 0)
                    {
                        while (!isStackEmpty(MathStack))
                        {
                            postfix[y] = PopOp(MathStack);
                            y++;
                        }

                        postfix[y] = '\0';

                        i = 0;
                        /* SECTION: 후위식 계산 루프 — 후위 문자열을 스캔하며 스택으로 계산 */
                        CalcStack->top = NULL;
                        while (postfix[i] != '\0')
                        {
                            if (isdigit((unsigned char)postfix[i]))
                            {
                                CalcStack = PushPostfix(postfix[i] - '0', CalcStack);
                            }
                            else if (postfix[i] == '+' || postfix[i] == '-' || postfix[i] == '*' || postfix[i] == '/')
                            {
                                val1 = PopPostfix(CalcStack);
                                val2 = PopPostfix(CalcStack);

                                switch (postfix[i])
                                {
                                case '+': resultVal = val2 + val1; break;
                                case '-': resultVal = val2 - val1; break;
                                case '/': resultVal = val2 / val1; break;
                                case '*': resultVal = val2 * val1; break;
                                }
                                CalcStack = PushPostfix(resultVal, CalcStack);
                            }
                            i++;
                        }

                        /* SECTION: 계산 결과 저장 — 마지막 식 결과를 전역값으로 보관 */
                        LastExpReturn = CalcStack->top->val;
                    }
                    WillBreak = 0;
                }
            }
        }
    }

    fclose(filePtr);
    STACK = FreeAll(STACK);

    printf("\nPress a key to exit...");
    getch();
    return 0;
}

static Stack* FreeAll(Stack* stck)
{
    Node* head = stck->top;
    while (head) {
        Node* temp = head;
        head = head->next;
        free(temp);
    }
    stck->top = NULL;
    return NULL;
}

static int GetLastFunctionCall(Stack* stck)
{
    Node* head = stck->top;
    while (head) {
        if (head->type == 3) return head->line;
        head = head->next;
    }
    return 0;
}

static int GetVal(char exp_name, int* line, Stack* stck)
{
    Node* head;
    *line = 0;
    if (stck->top == NULL) return -999;
    head = stck->top;
    while (head) {
        if (head->exp_data == exp_name)
        {
            if (head->type == 1) return head->val;
            else if (head->type == 2) { *line = head->line; return -1; }
        }
        head = head->next;
    }
    return -999;
}

static int my_stricmp(const char* a, const char* b)
{
    unsigned char ca, cb;
    while (*a || *b) {
        ca = (unsigned char)tolower((unsigned char)*a);
        cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb) return (int)ca - (int)cb;
        if (*a) a++;
        if (*b) b++;
    }
    return 0;
}

static void rstrip(char* s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
}
