type unary_op =
  | U_neg
  | U_pos
  | U_not
  | U_pull

type postfix_op =
  | Post_inc
  | Post_dec

type binary_op =
  | B_plus
  | B_minus
  | B_mul
  | B_div
  | B_mod
  | B_pow
  | B_union
  | B_intersection
  | B_eq
  | B_neq
  | B_lt
  | B_le
  | B_gt
  | B_ge
  | B_and
  | B_or
  | B_at

type expr =
  | Int of int
  | Float of float
  | Bool of bool
  | String of string
  | List of expr list
  | Set of expr list
  (* { name: "Emilio"; age: 21; } -- fields keep their source order. *)
  | Record of (string * expr) list
  | Null
  | Identifier of string
  | Unary of unary_op * expr
  | Postfix of postfix_op * expr
  | Binary of expr * binary_op * expr
  | Call of expr * expr list
  | Attribute of expr * string
  (* items[0], user["name"] -- read and assignment target. *)
  | Index of expr * expr
  | IfExpr of expr * expr * expr
  | Range of expr * expr
  | FunctionLiteral of function_literal
  (* The value of an expression, held to a declared type. 'int n = 5;' is
     'n = Annotated ("int", "n", 5)', and so is every later assignment to n: the
     parser writes it in, so the evaluator and the compiler are handed the same
     tree and cannot disagree about what a declaration means. The name is only
     there for the error message. *)
  | Annotated of string * string * expr

and statement =
  | ExprStmt of expr
  | Assign of expr * expr
  | Return of expr option
  | Break
  | Continue
  (* try { ... } else [name] { ... } -- the name binds the error record. *)
  | TryStmt of block * string option * block
  | IfStmt of expr * block * block option
  | MatchStmt of expr * (expr * block) list * block option
  | WhileStmt of expr * block
  | ForStmt of string * expr * block
  | FunctionDef of function_decl
  | Import of string
  | Gui of gui_block

and block = statement list

and function_decl = {
  name : string;
  params : string list;
  body : block;
}

and function_literal = {
  params : string list;
  body : block;
}

and gui_literal =
  | GuiString of string
  | GuiBool of bool
  | GuiNumber of float
  | GuiLength of int
  (* A share of whatever the parent has to give, as written: 50%. Resolved by
     the backend at layout time, since only then is the parent's size known. *)
  | GuiPercent of float
  (* A span of time in milliseconds, however it was written. *)
  | GuiDuration of int
  | GuiColor of string
  | GuiIdent of string

and gui_property_value =
  | GuiPropLiteral of gui_literal
  | GuiPropEvent of string

and gui_block = GuiBlock of string * gui_literal list * (string * gui_property_value) list * gui_block list

type program = statement list
