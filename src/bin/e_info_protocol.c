#include "e.h"
#include "e_info_protocol.h"

static E_Info_Rule_Checker *rc = NULL;

// should be increasing order for binary search
static struct
{
   const char * token_char;
   const int    token_length;
   E_Info_Token token_name;
} token_table[] =
{
   { "\0",  1, E_INFO_TOKEN_EOS }, // 0
   { "\t",  1, E_INFO_TOKEN_SPACE }, // 9
   { " ",   1, E_INFO_TOKEN_SPACE }, // 32
   { "!=",  2, E_INFO_TOKEN_NOT_EQ }, // 33 61
   { "&",   1, E_INFO_TOKEN_AND }, // 38
   { "&&",  2, E_INFO_TOKEN_AND }, // 38 38
   { "(",   1, E_INFO_TOKEN_L_BR }, // 40
   { ")",   1, E_INFO_TOKEN_R_BR }, // 41
   { "<",   1, E_INFO_TOKEN_LSS_THAN }, // 60
   { "<=",  2, E_INFO_TOKEN_LSS_EQ }, // 60 61
   { "<>",  2, E_INFO_TOKEN_NOT_EQ }, // 60 62
   { "=",   1, E_INFO_TOKEN_EQUAL }, // 61
   { "==",  2, E_INFO_TOKEN_EQUAL }, // 61 61
   { ">",   1, E_INFO_TOKEN_GRT_THAN }, // 62
   { ">=",  2, E_INFO_TOKEN_GRT_EQ }, // 62 61
   { "and", 3, E_INFO_TOKEN_AND }, // 97 110
   { "or",  2, E_INFO_TOKEN_OR }, // 111 114
   { "|",   1, E_INFO_TOKEN_OR }, // 124
   { "||",  2, E_INFO_TOKEN_OR }, // 124 124
};

typedef struct
{
   int         type;
   int         target_id;
   const char *name;
   int         pid;
   char       *cmd;
} E_Info_Validate_Args;

typedef struct
{
   char **reply;
   int   *len;
} E_Info_Reply_Buffer;

typedef enum
{
   E_INFO_RULE_SET_OK,
   E_INFO_RULE_SET_ERR_TOO_MANY_RULES,
   E_INFO_RULE_SET_ERR_PARSE,
   E_INFO_RULE_SET_ERR_NO_RULE
} E_Info_Rule_Set_Result;

static E_Info_Tree_Node * _e_info_parser_token_parse(E_Info_Tree *tree, E_Info_Token_Data *token);

E_Info_Tree *
_e_info_bintree_create_tree(int size)
{
   E_Info_Tree *tree = calloc(1, sizeof(E_Info_Tree) + size);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tree, NULL);

   tree->size = size;
   tree->head = NULL;

   return tree;
}

E_Info_Tree_Node *
_e_info_bintree_create_node(E_Info_Tree *tree)
{
   E_Info_Tree_Node *node = calloc(1, sizeof(E_Info_Tree_Node) + tree->size);
   EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

   node->left = NULL;
   node->right = NULL;

   return node;
}

E_Info_Tree_Node *
_e_info_bintree_get_head(E_Info_Tree *tree)
{
   return tree->head;
}

void
_e_info_bintree_set_head(E_Info_Tree *tree, E_Info_Tree_Node *head)
{
   tree->head = head;
}

void
_e_info_bintree_set_left_child(E_Info_Tree_Node *node, E_Info_Tree_Node *child)
{
   node->left = child;
}

void
_e_info_bintree_set_right_child(E_Info_Tree_Node *node, E_Info_Tree_Node *child)
{
   node->right = child;
}

E_Info_Tree_Node *
_e_info_bintree_get_left_child(E_Info_Tree_Node *node)
{
   return node->left;
}

E_Info_Tree_Node *
_e_info_bintree_get_right_child(E_Info_Tree_Node *node)
{
   return node->right;
}

void *
_e_info_bintree_get_node_data(E_Info_Tree_Node *node)
{
   return (void*)(node + 1);
}

void
_e_info_bintree_remove_node(E_Info_Tree_Node *node)
{
   free(node);
}

void
_e_info_bintree_remove_node_recursive(E_Info_Tree_Node *node)
{
   if (node->left)
     _e_info_bintree_remove_node_recursive(node->left);
   if (node->right)
     _e_info_bintree_remove_node_recursive(node->right);

   _e_info_bintree_remove_node(node);
}

void
_e_info_bintree_destroy_tree(E_Info_Tree *tree)
{
   if (tree->head)
     _e_info_bintree_remove_node_recursive(tree->head);

   free(tree);
}

static int
_e_info_bintree_inorder_traverse_recursive(E_Info_Tree *tree, E_Info_Tree_Node *node, E_Info_Tree_Node *parent, E_Info_Tree_Traverse_Cb func, void *arg)
{
   if (node->left)
     if (_e_info_bintree_inorder_traverse_recursive(tree, node->left, node, func, arg) != 0)
       return 1;

   if (func(tree, node, parent, arg))
     return 1;

   if (node->right)
     if (_e_info_bintree_inorder_traverse_recursive(tree, node->right, node, func, arg) != 0)
       return 1;

   return 0;
}

void
_e_info_bintree_inorder_traverse(E_Info_Tree *tree, E_Info_Tree_Traverse_Cb func, void *arg)
{
   if (tree->head)
     _e_info_bintree_inorder_traverse_recursive(tree, tree->head, tree->head, func, arg);
}

static int
_e_info_bintree_postorder_traverse_recursive(E_Info_Tree *tree, E_Info_Tree_Node *node, E_Info_Tree_Node *parent, E_Info_Tree_Traverse_Cb func, void *arg)
{
   if (node->left)
     if (_e_info_bintree_postorder_traverse_recursive(tree, node->left, node, func, arg) != 0)
       return 1;
   if (node->right)
     if (_e_info_bintree_postorder_traverse_recursive(tree, node->right, node, func, arg) != 0)
       return 1;

   return func(tree, node, parent, arg);
}

void
_e_info_bintree_postorder_traverse(E_Info_Tree *tree, E_Info_Tree_Traverse_Cb func, void *arg)
{
   if (tree->head)
     _e_info_bintree_postorder_traverse_recursive(tree, tree->head, tree->head, func, arg);
}

E_Info_Token
_e_info_parser_next_token_get(const char **string)
{
   static int token_cnt = sizeof(token_table) / sizeof(token_table[0]);
   int i, compare_res, found = 0, first, last;

   first = 0;
   last = token_cnt - 1;

   i = (first + last) / 2;
   while (1)
     {
        compare_res = strncmp(*string, token_table[i].token_char, token_table[i].token_length);
        while (compare_res == 0)
          {
             found = 1;
             i++;
             if (i == token_cnt)
               break;
             compare_res = strncmp(*string, token_table[i].token_char, token_table[i].token_length);
          }

        if (found)
          {
             i--;
             *string += token_table[i].token_length;

             return token_table[i].token_name;
          }

        if (first >= last)
          break;

        if (compare_res > 0)
          first = i + 1;
        else
          last = i - 1;

        i = (first + last) / 2;
     }

   if (isalpha(**string))
     {
        (*string)++;
        while (isalpha(**string) || isdigit(**string) || **string == '_' || **string == '-')
          (*string)++;

        return E_INFO_TOKEN_SYMBOL;
     }

   if (isdigit(**string))
     {
        (*string)++;
        while (isdigit(**string))
          (*string)++;

        return E_INFO_TOKEN_NUMBER;
    }

   return E_INFO_TOKEN_UNKNOWN;
}

static void
_e_info_parser_token_process(E_Info_Token_Data *token)
{
   do
     {
        token->last_symbol = *(token->string);
        token->last_token = _e_info_parser_next_token_get(token->string);
        token->symbol_len = *(token->string) - token->last_symbol;
     }
   while (token->last_token == E_INFO_TOKEN_SPACE);
}

static E_Info_Tree_Node *
_e_info_parser_statement_parse(E_Info_Tree *tree, E_Info_Token_Data *token)
{
   E_Info_Tree_Node *node = NULL;
   E_Info_Rule_Node *data;

   if (token->last_token == E_INFO_TOKEN_L_BR)
     {
        _e_info_parser_token_process(token);

        node = _e_info_parser_token_parse(tree, token);
        if (!node)
          return NULL;

        if (token->last_token != E_INFO_TOKEN_R_BR)
          goto fail;
        _e_info_parser_token_process(token);

        return node;
     }

   if (token->last_token != E_INFO_TOKEN_SYMBOL)
     goto fail;

   node = _e_info_bintree_create_node(tree);
   EINA_SAFETY_ON_NULL_GOTO(node, fail);

   data = (E_Info_Rule_Node *)_e_info_bintree_get_node_data(node);

   strncpy(data->variable_name, token->last_symbol, token->symbol_len);
   data->variable_name[token->symbol_len] = '\0';

   if (!strcasecmp(data->variable_name, "all"))
     {
        data->node_type = E_INFO_NODE_TYPE_ALL;
        _e_info_parser_token_process(token);

        return node;
     }

   data->node_type = E_INFO_NODE_TYPE_DATA;

   _e_info_parser_token_process(token);

   switch (token->last_token)
     {
      case E_INFO_TOKEN_NOT_EQ:
        data->comparer = E_INFO_COMPARER_NOT_EQ;
        break;
      case E_INFO_TOKEN_EQUAL:
        data->comparer = E_INFO_COMPARER_EQUAL;
        break;
      case E_INFO_TOKEN_LSS_THAN:
        data->comparer = E_INFO_COMPARER_LESS;
        break;
      case E_INFO_TOKEN_LSS_EQ:
        data->comparer = E_INFO_COMPARER_LESS_EQ;
        break;
      case E_INFO_TOKEN_GRT_THAN:
        data->comparer = E_INFO_COMPARER_GREATER;
        break;
      case E_INFO_TOKEN_GRT_EQ:
        data->comparer = E_INFO_COMPARER_GREATER_EQ;
        break;
      default:
        goto fail;
     }

   _e_info_parser_token_process(token);

   if (token->last_token == E_INFO_TOKEN_NUMBER)
     {
        data->value_type = E_INFO_DATA_TYPE_INTEGER;
        data->value.integer = atoi(token->last_symbol);
     }
   else if (token->last_token == E_INFO_TOKEN_SYMBOL)
     {
        data->value_type = E_INFO_DATA_TYPE_STRING;
        strncpy(data->value.string, token->last_symbol, token->symbol_len);
        data->value.string[token->symbol_len] = '\0';
     }
   else
     {
        goto fail;
     }

   _e_info_parser_token_process(token);

   return node;

fail:
   if (node)
     _e_info_bintree_remove_node_recursive(node);

   return NULL;
}

static E_Info_Tree_Node *
_e_info_parser_token_parse(E_Info_Tree *tree, E_Info_Token_Data *token)
{
   E_Info_Tree_Node *node, *left = NULL, *right = NULL;
   E_Info_Rule_Node *data;

   node = _e_info_parser_statement_parse(tree, token);
   if (!node)
     {
        printf("PARSE statement error\n");
        goto fail;
     }

   while (token->last_token == E_INFO_TOKEN_AND)
     {
        left = node;
        node = NULL;

        _e_info_parser_token_process(token);

        right = _e_info_parser_statement_parse(tree, token);
        if (!right)
          goto fail;

        node = _e_info_bintree_create_node(tree);
        EINA_SAFETY_ON_NULL_GOTO(node, fail);

        data = (E_Info_Rule_Node *)_e_info_bintree_get_node_data(node);
        data->node_type = E_INFO_NODE_TYPE_AND;
        _e_info_bintree_set_left_child(node, left);
        _e_info_bintree_set_right_child(node, right);
     }

   if (token->last_token == E_INFO_TOKEN_OR)
     {
        left = node;
        node = NULL;

        _e_info_parser_token_process(token);

        right = _e_info_parser_token_parse(tree, token);
        if (!right)
          goto fail;

        node = _e_info_bintree_create_node(tree);
        EINA_SAFETY_ON_NULL_GOTO(node, fail);

        data = (E_Info_Rule_Node *)_e_info_bintree_get_node_data(node);
        data->node_type = E_INFO_NODE_TYPE_OR;
        _e_info_bintree_set_left_child(node, left);
        _e_info_bintree_set_right_child(node, right);
     }

   return node;

fail:
   if (left)
     _e_info_bintree_remove_node_recursive(left);

   return NULL;
}

E_Info_Tree *
_e_info_parser_rule_string_parse(const char * string)
{
   E_Info_Tree *tree;
   E_Info_Tree_Node *node;
   E_Info_Token_Data token;

   token.string = &string;
   _e_info_parser_token_process(&token);

   tree = _e_info_bintree_create_tree(sizeof(E_Info_Rule_Node));
   if (!tree) return NULL;

   node = _e_info_parser_token_parse(tree, &token);
   if (!node)
     {
        _e_info_bintree_destroy_tree(tree);
        return NULL;
     }

   _e_info_bintree_set_head(tree, node);

   return tree;
}

static int
_e_info_rulechecker_string_compare(E_Info_Comparer comparer, char * str2, char * str1)
{
   int result = strcasecmp(str2, str1);

   switch (comparer)
     {
        case E_INFO_COMPARER_EQUAL:
          return result == 0;
        case E_INFO_COMPARER_LESS:
          return result < 0;
        case E_INFO_COMPARER_GREATER:
          return result > 0;
        case E_INFO_COMPARER_LESS_EQ:
          return result <= 0;
        case E_INFO_COMPARER_GREATER_EQ:
          return result >= 0;
        case E_INFO_COMPARER_NOT_EQ:
          return result != 0;
     }

   return 0;
}

static int
_e_info_rulechecker_int_compare(E_Info_Comparer comparer, int int2, int int1)
{
   switch (comparer)
     {
        case E_INFO_COMPARER_EQUAL:
          return int1 == int2;
        case E_INFO_COMPARER_LESS:
          return int1 < int2;
        case E_INFO_COMPARER_GREATER:
          return int1 > int2;
        case E_INFO_COMPARER_LESS_EQ:
          return int1 <= int2;
        case E_INFO_COMPARER_GREATER_EQ:
          return int1 >= int2;
        case E_INFO_COMPARER_NOT_EQ:
          return int1 != int2;
     }

   return 0;
}

static int
_rule_print_func(E_Info_Tree *tree, E_Info_Tree_Node *node, E_Info_Tree_Node *parent, void *arg)
{
   E_Info_Reply_Buffer *buffer = (E_Info_Reply_Buffer*)arg;
   char *reply = *buffer->reply;
   int *len = buffer->len;
   char *operators[] = { "==", "<", ">", "<=", ">=", "!=" };
   E_Info_Rule_Node *data;

   data = _e_info_bintree_get_node_data(node);

   if (data->node_type == E_INFO_NODE_TYPE_ALL)
     REPLY("ALL");
   else if (data->node_type == E_INFO_NODE_TYPE_AND)
     REPLY(" and ");
   else if (data->node_type == E_INFO_NODE_TYPE_OR)
     REPLY(" or ");
   else // data->node_type == E_INFO_NODE_TYPE_DATA
     {
        if (node == _e_info_bintree_get_left_child(parent))
          REPLY("(");

        REPLY("%s %s ", data->variable_name, operators[data->comparer]);

        if (data->value_type == E_INFO_DATA_TYPE_INTEGER)
          REPLY("%d", data->value.integer);
        else
          REPLY("%s", data->value.string);

        if (node == _e_info_bintree_get_right_child(parent))
          REPLY(")");
     }

   *buffer->reply = reply;

   return 0;
}

static int
_rule_validate_func(E_Info_Tree *tree, E_Info_Tree_Node *node, E_Info_Tree_Node *parent, void *arg)
{
   E_Info_Validate_Args *args = (E_Info_Validate_Args *)arg;
   E_Info_Tree_Node *left, *right;
   E_Info_Rule_Node *data, *left_data= NULL, *right_data = NULL;

   data = _e_info_bintree_get_node_data(node);
   data->result = E_INFO_RESULT_UNKNOWN;

   if (data->node_type == E_INFO_NODE_TYPE_AND || data->node_type == E_INFO_NODE_TYPE_OR)
     {
        left = _e_info_bintree_get_left_child(node);
        right = _e_info_bintree_get_right_child(node);
        if (!left || !right)
          {
             printf("Node error\n");
             return -1;
          }

        left_data = _e_info_bintree_get_node_data(left);
        right_data = _e_info_bintree_get_node_data(right);
     }

   if (data->node_type == E_INFO_NODE_TYPE_ALL)
     {
        data->result = E_INFO_RESULT_TRUE;
     }
   else if (data->node_type == E_INFO_NODE_TYPE_DATA)
     {
        char iface[64];
        char *msg = NULL;

        if (args->name)
          msg = index(args->name, ':');
        if (msg)
          {
             int min = MIN(sizeof(iface)-1, msg-args->name);
             strncpy(iface, args->name, min);
             iface[min] = '\0';
             msg++;
          }
        if (!strcasecmp(data->variable_name, "TYPE"))
          {
             char *type_string;
             if (args->type == 0)
               type_string = "REQUEST";
             else if (args->type == 1)
               type_string = "EVENT";
             else
               {
                  fprintf (stderr, "Invalid type %d\n", args->type);
                  return -1;
               }

             if (_e_info_rulechecker_string_compare(data->comparer, data->value.string, type_string))
               data->result = E_INFO_RESULT_TRUE;
             else
               data->result = E_INFO_RESULT_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "IFACE"))
          {
             if (msg && _e_info_rulechecker_string_compare(data->comparer, data->value.string, iface))
               data->result = E_INFO_RESULT_TRUE;
             else
               data->result = E_INFO_RESULT_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "MSG"))
          {
             if (msg && _e_info_rulechecker_string_compare(data->comparer, data->value.string, msg))
               data->result = E_INFO_RESULT_TRUE;
             else
               data->result = E_INFO_RESULT_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "PID"))
          {
             if (_e_info_rulechecker_int_compare(data->comparer, data->value.integer, args->pid))
               data->result = E_INFO_RESULT_TRUE;
             else
               data->result = E_INFO_RESULT_FALSE;
          }
        else if (!strcasecmp(data->variable_name, "CMD") || !strcasecmp(data->variable_name, "COMMAND"))
          {
             if (args->cmd && _e_info_rulechecker_string_compare(data->comparer, data->value.string, args->cmd))
               data->result = E_INFO_RESULT_TRUE;
             else
               data->result = E_INFO_RESULT_FALSE;
          }
     }
   else if (data->node_type == E_INFO_NODE_TYPE_AND)
     {
        if (left_data->result == E_INFO_RESULT_TRUE && right_data->result == E_INFO_RESULT_TRUE)
          data->result = E_INFO_RESULT_TRUE;
        else
          data->result = E_INFO_RESULT_FALSE;
     }
   else if (data->node_type == E_INFO_NODE_TYPE_OR)
     {
        if (left_data->result == E_INFO_RESULT_TRUE || right_data->result == E_INFO_RESULT_TRUE)
          data->result = E_INFO_RESULT_TRUE;
        else
          data->result = E_INFO_RESULT_FALSE;
     }
   else
     return -1;

   return 0;
}

E_Info_Rule_Checker *
_e_info_rulechecker_init()
{
   E_Info_Rule_Checker *rc = calloc(1, sizeof (E_Info_Rule_Checker));
   if (!rc)
     return NULL;

   rc->count = 0;

   return rc;
}

E_Info_Rule_Set_Result
_e_info_rulechecker_rule_add(E_Info_Rule_Checker *rc, E_Info_Policy_Type policy, const char *rule_string)
{
   if (rc->count == MAX_RULE)
     return E_INFO_RULE_SET_ERR_TOO_MANY_RULES;

   rc->rules[rc->count].tree = _e_info_parser_rule_string_parse(rule_string);
   if (!rc->rules[rc->count].tree)
     return E_INFO_RULE_SET_ERR_PARSE;

   rc->rules[rc->count].policy = policy;
   rc->count++;

   return E_INFO_RULE_SET_OK;
}

E_Info_Rule_Set_Result
_e_info_rulechecker_rule_remove(E_Info_Rule_Checker *rc, int index)
{
   if (index < 0 || index >= rc->count)
     return E_INFO_RULE_SET_ERR_NO_RULE;

   _e_info_bintree_destroy_tree(rc->rules[index].tree);
   rc->count--;
   if (index != rc->count)
      memmove(&rc->rules[index], &rc->rules[index + 1], sizeof (E_Info_Rule) * (rc->count - index));

   return E_INFO_RULE_SET_OK;
}

void
_e_info_rulechecker_rule_print(E_Info_Rule_Checker *rc, char *reply, int *len)
{
   E_Info_Reply_Buffer buffer = {&reply, len};
   int i;

   REPLY(" --------------------------[ Protocol Filter Rules ]--------------------------\n");
   REPLY("  No      Policy              Rule\n");
   REPLY(" -----------------------------------------------------------------------------\n");

   for (i = 0; i < rc->count; i++)
     {
        REPLY(" %3d %10s \"", i, rc->rules[i].policy == E_INFO_POLICY_TYPE_ALLOW ? "ALLOW" : "DENY");
        _e_info_bintree_inorder_traverse(rc->rules[i].tree, _rule_print_func, (void*)&buffer);
        REPLY("\"\n");
     }
}

const char *
_e_info_rulechecker_usage_print()
{
   return
        "##########################################################\n"
        "###     Enlightenment Protocol Log filtering.          ###\n"
        "##########################################################\n"
        "\n"
        "-----------------------------------------------------------------\n"
        "How to read enlightenment_info protocol messages :\n"
        "[timestamp]   Server --> Client [PID: [pid]] interface@id.message(arguments..) cmd: CMD\n"
        "  ex)\n"
        "[1476930.145] Server --> Client [PID: 103] wl_touch@10.down(758, 6769315, wl_surface@23, 0, 408.000, 831.000) cmd: /usr/bin/launchpad-loader\n"
        "             ==> type = event && pid = 103 && cmd = launchpad-loader && iface = wl_touch && msg = up\n"
        "[4234234.123] Server <-- Client [PID: 123] wl_seat@32.get_touch(new id wl_touch@22) cmd: /usr/bin/launchpad-loader\n"
        "             ==> type = request && pid = 123 && cmd = launchpad-loader && iface = wl_seat && msg = get_touch\n"
        "-----------------------------------------------------------------\n"
        "Usage : enlightenment_info -protocol_rule add [POLICY] [RULE]\n"
        "        enlightenment_info -protocol_rule remove [INDEX]\n"
        "        enlightenment_info -protocol_rule file [RULE_FILE]\n"
        "        enlightenment_info -protocol_rule print\n"
        "        enlightenment_info -protocol_rule help\n"
        "        [POLICY] : allow / deny \n"
        "        [RULE] : C Language-style boolean expression syntax. [VARIABLE] [COMPAROTOR] [VALUE]\n"
        "        [VARIABLE] : type / iface / msg / cmd(command) / pid\n"
        "        [COMPARATOR] : & / && / and / | / || / or / = / == / != / > / >= / < / <=\n"
        "        [VALUE] : string / number  \n"
        "  ex)\n"
        "        enlightenment_info -protocol_rule add allow \"(type=request) && (iface == wl_pointer and (msg = down or msg = up))\"\n"
        "        enlightenment_info -protocol_rule add deny cmd!= launch-loader\n"
        "        enlightenment_info -protocol_rule remove all\n"
        "        enlightenment_info -protocol_rule remove 3\n"
        "\n";
}

void
_e_info_rulechecker_destroy(E_Info_Rule_Checker *rc)
{
   int i;

   for (i = rc->count - 1; i >= 0; i--)
     _e_info_rulechecker_rule_remove(rc, i);

   free(rc);
}

static int
_e_info_rulechecker_rule_validate(E_Info_Rule_Checker *rc, int type, int target_id, const char * name, int pid, char *cmd)
{
   E_Info_Validate_Args args = { type, target_id, name, pid, cmd };
   E_Info_Tree_Node *node;
   E_Info_Rule_Node *data;
   E_Info_Policy_Type default_policy = E_INFO_POLICY_TYPE_DENY;
   int i;

   for (i = rc->count - 1; i >= 0; i--)
     {
        _e_info_bintree_postorder_traverse(rc->rules[i].tree, _rule_validate_func, &args);
        node = (E_Info_Tree_Node *)_e_info_bintree_get_head(rc->rules[i].tree);
        data = (E_Info_Rule_Node *)_e_info_bintree_get_node_data(node);

        if (data->result == E_INFO_RESULT_TRUE)
          return rc->rules[i].policy == E_INFO_POLICY_TYPE_ALLOW;
     }

   return default_policy == E_INFO_POLICY_TYPE_ALLOW;
}

static void
_e_info_protocol_arguments_merge(char *target, int target_size, int argc, const char **argv)
{
   int i, len;

   for (i = 0; i < argc; i++)
     {
        len = snprintf(target, target_size, "%s", argv[i]);
        target += len;
        target_size -= len;

        if (i != argc - 1)
          {
             *(target++) = ' ';
             target_size--;
          }
     }
}

char *
_e_info_protocol_cmd_get(char *path)
{
   char *p;

   if (!path) return NULL;

   p = strrchr(path, '/');

   return (p) ? p + 1 : path;
}

Eina_Bool
_e_info_protocol_rule_file_set(const char *filename, char *reply, int *len)
{
   int   fd = -1, rule_len;
   char  fs[8096], *pfs;

   fd = open(filename, O_RDONLY);
   if (fd < 0)
     {
        REPLY("failed: open '%s'\n", filename);
        return EINA_FALSE;
     }

   rule_len = read(fd, fs, sizeof(fs));
   pfs = fs;

   while (pfs - fs < rule_len)
     {
        int   i, new_argc = 3;
        char *new_argv[3] = {"add", };
        char  policy[64] = {0, };
        char  rule[1024] = {0, };

        if (pfs[0] == ' ' || pfs[0] == '\n')
          {
             pfs++;
             continue;
          }
        for (i = 0 ; pfs[i] != ' ' ; i++)
          policy[i] = pfs[i];

        new_argv[1] = policy;
        pfs += (strlen(new_argv[1]) + 1);

        memset(rule, 0, sizeof(rule));
        for (i = 0 ; pfs[i] != '\n' ; i++)
          rule[i] = pfs[i];

        new_argv[2] = rule;

        pfs += (strlen(new_argv[2]) + 1);

        if (!e_info_protocol_rule_set((const int) new_argc, (const char**) new_argv, reply, len))
          {
             close(fd);
             return EINA_FALSE;
          }

     }

   close(fd);

   return EINA_TRUE;
}

Eina_Bool
e_info_protocol_rule_set(const int argc, const char **argv, char *reply, int *len)
{
   const char * command;

   if (argc == 0)
     {
        _e_info_rulechecker_rule_print(rc, reply, len);
        return EINA_TRUE;
     }

   command = argv[0];

   if (!strcasecmp(command, "add"))
     {
        E_Info_Policy_Type policy_type;
        E_Info_Rule_Set_Result result;
        const char * policy = argv[1];
        char merge[8192] = {0,}, rule[8192] = {0,};
        int i, index = 0, size_rule, apply = 0;

        if (argc < 3)
          {
             REPLY("Error : Too few arguments.\n");
             return EINA_FALSE;
          }

        if (!strcasecmp(policy, "ALLOW"))
          policy_type = E_INFO_POLICY_TYPE_ALLOW;
        else if (!strcasecmp(policy, "DENY"))
          policy_type = E_INFO_POLICY_TYPE_DENY;
        else
          {
             REPLY("Error : Unknown policy : [%s].\n          Policy should be ALLOW or DENY.\n", policy);
             return EINA_FALSE;
          }

        _e_info_protocol_arguments_merge(merge, sizeof(merge), argc - 2, &(argv[2]));

        size_rule = sizeof(rule) - 1;

        for (i = 0 ; i < strlen(merge) ; i++)
          {
             if (merge[i] == '\"' || merge[i] == '\'')
               {
                  rule[index++] = ' ';
                  if (index > size_rule)
                    return EINA_FALSE;

                  continue;
               }

             if (merge[i] == '+')
               {
                  rule[index++] = ' ';
                  if (index > size_rule)
                    return EINA_FALSE;

                  if (apply == 0)
                    {
                       const char* plus = "|| type=reply || type=error";
                       int len = MIN(size_rule - index, strlen(plus));
                       strncat(rule, plus, len);
                       index += len;
                       if (index > size_rule)
                         return EINA_FALSE;

                       apply = 1;
                    }
                  continue;
               }
             rule[index++] = merge[i];
             if (index > size_rule)
               return EINA_FALSE;
          }

        result = _e_info_rulechecker_rule_add(rc, policy_type, rule);
        if (result == E_INFO_RULE_SET_ERR_TOO_MANY_RULES)
          {
             REPLY("Error : Too many rules were added.\n");
             return EINA_FALSE;
          }
        else if (result == E_INFO_RULE_SET_ERR_PARSE)
          {
             REPLY("Error : An error occured during parsing the rule [%s]\n", rule);
             return EINA_FALSE;
          }

        REPLY("The rule was successfully added.\n");
        _e_info_rulechecker_rule_print(rc, reply, len);

        return EINA_TRUE;
     }
   else if (!strcasecmp(command, "remove"))
     {
        const char * remove_idx;
        int i;

        if (argc < 2)
          {
             REPLY("Error : Too few arguments.\n");
             return EINA_FALSE;
          }

        for (i = 0; i < argc - 1; i++)
          {
             remove_idx = argv[i + 1];

             if (!strcasecmp(remove_idx, "all"))
               {
                  _e_info_rulechecker_destroy(rc);
                  rc = _e_info_rulechecker_init();
                  if (!rc)
                    {
                       REPLY("Error : rules not removed\n");
                       return EINA_FALSE;
                    }
                  REPLY("Every rules were successfully removed.\n");
               }
             else
               {
                  int index = atoi(remove_idx);
                  if (isdigit(*remove_idx) && _e_info_rulechecker_rule_remove(rc, index) == 0)
                    REPLY("The rule [%d] was successfully removed.\n", index);
                  else
                    REPLY("Rule remove fail : No such rule [%s].\n", remove_idx);
               }
          }
        _e_info_rulechecker_rule_print(rc, reply, len);

        return EINA_TRUE;
     }
   else if (!strcasecmp(command, "file"))
     {
        if (argc < 2)
          {
             REPLY("Error : Too few arguments.\n");
             return EINA_FALSE;
          }

        if (!_e_info_protocol_rule_file_set(argv[1], reply, len))
          return EINA_FALSE;
        _e_info_rulechecker_rule_print(rc, reply, len);

        return EINA_TRUE;
     }
   else if (!strcasecmp(command, "print"))
     {
        _e_info_rulechecker_rule_print (rc, reply, len);

        return EINA_TRUE;
     }
   else if (!strcasecmp(command, "help"))
     {
        REPLY("%s", _e_info_rulechecker_usage_print());

        return EINA_TRUE;
     }

   REPLY("%s\nUnknown command : [%s].\n\n", _e_info_rulechecker_usage_print(), command);

   return EINA_TRUE;
}

Eina_Bool
e_info_protocol_rule_validate(E_Info_Protocol_Log *log)
{
   char *cmd = "";

   if (!rc)
     return EINA_FALSE;

   cmd = _e_info_protocol_cmd_get(log->cmd);

   return _e_info_rulechecker_rule_validate(rc,
                                            log->type,
                                            log->target_id,
                                            log->name,
                                            log->client_pid,
                                            cmd);
}

void
e_info_protocol_init()
{
   rc = _e_info_rulechecker_init();
}

void
e_info_protocol_shutdown()
{
   if (rc)
     _e_info_rulechecker_destroy(rc);
}
