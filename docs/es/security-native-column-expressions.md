# Permisos de columnas SQL nativo antes de evaluar

Para usuarios identificados del diccionario con grants SELECT por columna, la
autorización comprueba las referencias antes de leer filtros, ordenar, agrupar,
agregar, evaluar CASE, funciones, aritmética y entradas de ventanas. El acceso
por nombre de archivo usa el mismo objeto del diccionario que el alias. SELECT *
simple sigue proyectando solo columnas permitidas. No cambia admin ni fuentes
sin restricciones de columnas.

Cambio intencional: joins con fuentes restringidas, subconsultas inline con una
fuente o fila externa restringida, expresiones script, llamadas escalares
anidadas y comodines complejos se rechazan con error nativo 7079 (envoltorio SQL
7200). El ejecutor actual no tiene un origen fiable por columna para estas formas;
el permiso SELECT de tabla no protege por sí solo sus columnas ocultas. Usar
consultas explícitas de una fuente con columnas permitidas, o revisar los grants
con un administrador. No implica cobertura completa de APIs de tabla no SQL ni
de backends SQL externos.

Los cursores simples restringidos también se materializan físicamente con solo
campos permitidos, en lugar de devolver la tabla real con proyección visual.
Leer registro raw, pedir campo por nombre o cambiar filtros no recupera campos
omitidos del resultado. Si falla materializar, no se devuelve la fuente real.
