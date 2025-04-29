valgrind --tool=memcheck \
         --leak-check=full \
         --show-leak-kinds=definite,possible \
         --track-origins=yes \
         --num-callers=20 \
         --log-file=valgrind.log \
         ./cupidfm <<<'q'      # quit immediately (or script a minimal session)
