# Presupuesto de trabajo compartido de scripts

Todos los Executors anidados síncronos por hilo comparten un millón de pasos
y un plazo cooperativo de cinco segundos. Sentencias, iteraciones y evaluación
de expresiones consumen pasos. Otro Executor creado por procedimiento, trigger
o UDF no reinicia el contador. Se permiten ocho niveles de Executor.
El agotamiento atraviesa CATCH; una ejecución independiente posterior empieza
con presupuesto nuevo. Los límites afectan a scripts locales y remotos.

El reloj se comprueba cada 256 pasos, sin interrumpir forzosamente el hilo.
DLL nativas, scans SQL y backends externos pueden bloquear dentro del bridge:
este límite no los cancela mientras trabajan. Los efectos previos al fallo
no se revierten automáticamente. Usar transacción cuando sea necesario.
