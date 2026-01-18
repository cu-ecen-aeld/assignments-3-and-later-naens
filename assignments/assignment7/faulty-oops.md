Unable to handle kernel NULL pointer dereference.
The line 
	pc : faulty_write+0x10/0x20 [faulty]
shows that the dereference happened in the faulty_write function.
Since we did not compile with line numbers, we cannot say where exactly it
occurred.
But expecting the function itself,
    ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
                    loff_t *pos)
    {
            /* make a simple fault by dereferencing a NULL pointer */
            *(int *)0 = 0;
            return 0;
    }
we see that there are only two lines of code, the last being
	return 0;
which is not likely to generate an error.
So we can say that the line that caused the error is 
            *(int *)0 = 0;
.
Additional info:
 * objdump is at buildroot/output/host/bin/aarch64-linux-objdump
 * the module is at buildroot/output/target/lib/modules/6.1.44/extra/faulty.ko
 * the output of `objdump -S` for the function faulty_write is:
    0000000000000000 <faulty_write>:
       0:	d2800001 	mov	x1, #0x0                   	// #0
       4:	d2800000 	mov	x0, #0x0                   	// #0
       8:	d503233f 	paciasp
       c:	d50323bf 	autiasp
      10:	b900003f 	str	wzr, [x1]
      14:	d65f03c0 	ret
      18:	d503201f 	nop
      1c:	d503201f 	nop
