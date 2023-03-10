#define NODETYPES_IMPLEMENTATION
#include <assert.h>
#include <stdlib.h>
#include <vslc.h>

/* Global root for parse tree and abstract syntax tree */
node_t *root;

// Tasks
static void node_print(node_t *node, int nesting);
static void node_finalize(node_t *discard);
static void destroy_subtree(node_t *discard);
static node_t *simplify_tree(node_t *node);
static node_t *constant_fold_expression(node_t *node);
static node_t *replace_for_statement(node_t *for_node);

/* External interface */
void print_syntax_tree() {
  if (getenv("GRAPHVIZ_OUTPUT") != NULL)
    graphviz_node_print(root);
  else
    node_print(root, 0);
}

void simplify_syntax_tree(void) { root = simplify_tree(root); }

void destroy_syntax_tree(void) {
  destroy_subtree(root);
  root = NULL;
}

/* Initialize a node with type, data, and children */
void node_init(node_t *nd, node_type_t type, void *data, uint64_t n_children,
               ...) {
  va_list child_list;
  *nd = (node_t){.type = type,
                 .data = data,
                 .symbol = NULL,
                 .n_children = n_children,
                 .children = (node_t **)malloc(n_children * sizeof(node_t *))};
  va_start(child_list, n_children);
  for (uint64_t i = 0; i < n_children; i++)
    nd->children[i] = va_arg(child_list, node_t *);
  va_end(child_list);
}

/* Inner workings */
/* Prints out the given node and all its children recursively */
static void node_print(node_t *node, int nesting) {
  if (node != NULL) {
    printf("%*s%s", nesting, "", node_strings[node->type]);
    if (node->type == IDENTIFIER_DATA || node->type == STRING_DATA ||
        node->type == EXPRESSION || node->type == RELATION)
      printf("(%s)", (char *)node->data);
    else if (node->type == NUMBER_DATA)
      printf("(%ld)", *((int64_t *)node->data));
    putchar('\n');
    for (int64_t i = 0; i < node->n_children; i++)
      node_print(node->children[i], nesting + 1);
  } else
    printf("%*s%p\n", nesting, "", node);
}

/* Frees the memory owned by the given node, but does not touch its children */
static void node_finalize(node_t *discard) {
  if (discard != NULL) {
    free(discard->data);
    free(discard->children);
    free(discard);
  }
}

/* Recursively frees the memory owned by the given node, and all its children */
static void destroy_subtree(node_t *discard) {
  if (discard != NULL) {
    for (uint64_t i = 0; i < discard->n_children; i++)
      destroy_subtree(discard->children[i]);
    node_finalize(discard);
  }
}

/* Recursive function to convert a parse tree into an abstract syntax tree */
static node_t *simplify_tree(node_t *node) {
  if (node == NULL) return NULL;

  node_t *temp;

  // Simplify everything is the node's subtree before proceeding
  for (uint64_t i = 0; i < node->n_children; i++)
    node->children[i] = simplify_tree(node->children[i]);

  switch (node->type) {
    // TODO: Task 2.1
    // Eliminate nodes of purely syntactic value.
    // These nodes only have one child, and carry no semantic value.
    // PROGRAM, PRINT_ITEM, GLOBAL
    case PROGRAM:
    case GLOBAL:
    case PRINT_ITEM:
    case STATEMENT:
    case ARGUMENT_LIST:
      if (node->n_children != 1) break;
      temp = node->children[0];
      node_finalize(node);
      node = temp;
      break;

    // For nodes that only serve as a wrapper for a (optional) node below,
    // you may squash the child and take over its children instead.
    case PRINT_STATEMENT:
    case DECLARATION:
    case PARAMETER_LIST:
    case ARRAY_DECLARATION:
      if (node->n_children != 1) break;
      temp = node->children[0];
      temp->type = node->type;
      node_finalize(node);
      node = temp;
      break;

    // TODO: Task 2.2
    // Flatten linked list structures.
    // Any list node with two children, has a list node to the left, and an
    // element to the right. We return the left list node, but with the right
    // node appended to its list
    case PRINT_LIST:
    case VARIABLE_LIST:
    case STATEMENT_LIST:
    case DECLARATION_LIST:
    case GLOBAL_LIST:
    case EXPRESSION_LIST:
      if (node->n_children == 2 && node->children[0]->type == node->type) {
        temp = node->children[0];

        temp->n_children++;
        temp->children =
            realloc(temp->children, temp->n_children * sizeof(node_t *));
        temp->children[temp->n_children - 1] = node->children[1];

        node_finalize(node);
        node = temp;
      }
      break;

    // Do contstant folding, if possible
    // Also prunes expressions that are just wrapping atomic expressions
    case EXPRESSION:
      return constant_fold_expression(node);

    case FOR_STATEMENT:
      return replace_for_statement(node);

    default:
      break;
  }

  return node;
}

// Helper macros for manually building an AST
#define NODE(variable_name, ...)                  \
  node_t *variable_name = malloc(sizeof(node_t)); \
  node_init(variable_name, __VA_ARGS__)
// After an IDENTIFIER_NODE has been added to the tree, it can't be added again
// This macro replaces the given variable with a new node, containting a copy of
// the data
#define DUPLICATE_VARIABLE(variable)                     \
  do {                                                   \
    char *identifier = strdup(variable->data);           \
    variable = malloc(sizeof(node_t));                   \
    node_init(variable, IDENTIFIER_DATA, identifier, 0); \
  } while (false)
#define FOR_END_VARIABLE "123!@#__FOR_END__#@!321"

static node_t *constant_fold_expression(node_t *node) {
  assert(node->type == EXPRESSION);

  // TODO: Task 2.3
  // Replace expression nodes by their values, if it is possible to compute them
  // now

  // First, expression nodes with no operator, and only one child, are just
  // wrappers for
  //   NUMBER_DATA, IDENTIFIER_DATA or ARRAY_INDEXING, and can be replaced by
  //   its children

  if (node->n_children == 1 && node->children[0]->type == NUMBER_DATA) {
    node_t *temp = node->children[0];
    if (node->data != NULL && strcmp(node->data, "-") == 0 &&
        temp->data != NULL) {
      *((int64_t *)temp->data) *= -1;
    }
    node_finalize(node);
    node = temp;
  } else if (node->n_children == 1 && node->data == NULL) {
    node_t *temp = node->children[0];
    node_finalize(node);
    node = temp;
  } else if (node->n_children == 2 && node->children[0]->type == NUMBER_DATA &&
             node->children[1]->type == NUMBER_DATA) {
    node_t *temp = node->children[0];
    int64_t *x = temp->data;
    int64_t *y = node->children[1]->data;

    switch (((char *)(node->data))[0]) {
      case '+':
        *x = *x + *y;
        break;
      case '-':
        *x = *x - *y;
        break;
      case '*':
        *x = *x * *y;
        break;
      case '/':
        *x = *x / *y;
        break;
      default:
        break;
    }

    node_finalize(node->children[1]);
    node_finalize(node);
    node = temp;
  }

  // For expression nodes that are operators, we can only do constant folding if
  // all its children are NUMBER_DATA. In such cases, the expression node can be
  // replaced with the value of its operator, applied to its child(ren).

  // Remember to free up the memory used by the original node(s), if they get
  // replaced by a new node
  return node;
}

// Replaces the FOR_STATEMENT with a BLOCK.
// The block contains varables, setup, and a while loop
static node_t *replace_for_statement(node_t *for_node) {
  assert(for_node->type == FOR_STATEMENT);

  // extract child nodes from the FOR_STATEMENT
  node_t *variable = for_node->children[0];
  node_t *start_value = for_node->children[1];
  node_t *end_value = for_node->children[2];
  node_t *body = for_node->children[3];

  // TODO: Task 2.4
  // Replace the FOR_STATEMENT node, by instead creating the syntax tree of
  // an equivalent block with a while-statement As an example, the following
  // statement:
  //
  // for i in 5..N+1
  //     print a[i]
  //
  // should become:
  //
  // begin
  //     var i, __FOR_END__
  //     i := 5
  //     __FOR_END__ := N+1
  //     while i < __FOR_END__ begin
  //         print a[i]
  //         i := i + 1
  //     end
  // end
  //

  // To aid in the manual creation of AST nodes, you can create named nodes
  // using the NODE macro As an example, the following creates the var
  // <variable>, __FOR_END__ part of the transformation
  NODE(end_variable, IDENTIFIER_DATA, strdup(FOR_END_VARIABLE), 0);
  NODE(variable_list, VARIABLE_LIST, NULL, 2, variable, end_variable);
  NODE(declaration, DECLARATION, NULL, 1, variable_list);
  NODE(declaration_list, DECLARATION_LIST, NULL, 1, declaration);

  // An important thing to note, is that nodes may not be re-used
  // since that will cause errors when freeing up the syntax tree later.
  // because we want to use a lot of IDENTIFIER_DATA nodes with the same data,
  // we have the macro
  DUPLICATE_VARIABLE(variable);
  // Now we can use <variable> again, and it will be a new node for the same
  // identifier!
  NODE(init_assignment, ASSIGNMENT_STATEMENT, NULL, 2, variable, start_value);
  // We do the same whenever we want to reuse <end_variable> as well
  DUPLICATE_VARIABLE(end_variable);
  NODE(end_assignment, ASSIGNMENT_STATEMENT, NULL, 2, end_variable, end_value);

  // TODO: The rest is up to you. Good luck!
  // Don't fret if this part gets too cumbersome. Try your best

  // Create While Relation (variable < __FOR_END__)
  DUPLICATE_VARIABLE(variable);
  DUPLICATE_VARIABLE(end_variable);
  NODE(while_relation, RELATION, strdup("<"), 2, variable, end_variable);

  // Create assignment expression
  DUPLICATE_VARIABLE(variable);
  int64_t *value = malloc(sizeof(int64_t));
  *value = 1;
  NODE(while_assignment_expression_increment, NUMBER_DATA, value, 0);
  NODE(while_assignment_expression, EXPRESSION, strdup("+"), 2, variable,
       while_assignment_expression_increment);

  // Create assignment statement
  DUPLICATE_VARIABLE(variable);
  NODE(while_assignment_statement, ASSIGNMENT_STATEMENT, NULL, 2, variable,
       while_assignment_expression);

  // Add while_assignment_statement to body
  NODE(while_statement_list, STATEMENT_LIST, NULL, 2, body,
       while_assignment_statement);

  // Create while block containing updated body
  NODE(while_block, BLOCK, NULL, 1, while_statement_list);

  // Create new while statement
  NODE(while_statement, WHILE_STATEMENT, NULL, 2, while_relation, while_block);

  // Create statement list containing initialization statements (assingments),
  // and while statement
  NODE(statement_list, STATEMENT_LIST, NULL, 3, init_assignment, end_assignment,
       while_statement);

  // Create block containing variable declation and statement lists
  NODE(block, BLOCK, NULL, 2, declaration_list, statement_list);

  // TODO: Instead of returning the original for_node, destroy it, and return
  // your equivalent block
  node_finalize(for_node);
  return block;
}
