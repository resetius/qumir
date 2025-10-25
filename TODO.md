3. check if variables were assigned before use
4. check output arguments
5. check arg count on call
6. fix error (ожидалось число или)
8. different lowering for top level and function level
9. replace ops with functions (op ** )
10. create generic ast visitors and transformators
12. assignment to undefined identifier: return (for void functions)
13. implement arrays
14. implement function args attributes (i.e. in, out, in/out)
15. problem with Lookup russian vars (like лит строка; строка := "str")
16. lexer : empty string ("")
17. infinite loop on assignment of call non-arg function (i.e. s := f, were f is function void -> int)
18. Don't generate str_release for uninitialized str vars
19. Support lower string type (i.e. i8*)
