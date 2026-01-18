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
