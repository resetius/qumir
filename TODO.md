3. check if variables were assigned before use
4. check output arguments
5. check arg count on call
6. fix error (ожидалось число или)
12. assignment to undefined identifier: return (for void functions)
18. Don't generate str_release for uninitialized str vars
19. Expression must be assigned to a var
20. string[index] -> symbol (not substring)
22. slice/index out of bounds -> runtime error
23. add support: дано, надо, утв
24. support array cells as ref arguments (not supported by kumir)
25. add types support: лог, сим
26. add declr + init (i.e. цел i = 10)
27. allow names with double underscore
28. support https://github.com/Tapeline/goylang/blob/main/goylang.kum