==============================================
spice file netlist fixer
==============================================

HOW TO USE
===============

Just run ``make``, and execute ``build/smic180bcd_cdl_fixer``

.. code-block:: text

    make

Normally could see

.. code-block:: text

    Building  -> pre-build ...
    Building smic180bcd_cdl_fixer.c -> build/smic180bcd_cdl_fixer.o ...
    gcc -c -o build/smic180bcd_cdl_fixer.o smic180bcd_cdl_fixer.c -I. -lm
    gcc -o build/smic180bcd_cdl_fixer build/smic180bcd_cdl_fixer.o -I. -lm
    Building  -> post-build ..

And then, run ``smic180bcd_cdl_fixer`` in build folder, you could get

.. code-block:: text

    ./build/smic180bcd_cdl_fixer < orig.cdl > new.cdl
