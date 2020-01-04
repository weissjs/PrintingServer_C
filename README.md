# PrintingServer_C

This project was assigned for an Operating Systems course. Students were instructed to use processes and threads to emulate
'x' amount of printers and 'n' amount of devices which each have a random amount of randomly sized printing jobs. The size
of the printing queue is limited due to space, so sempahores were used to create mutex locks on the queue until there was open space.
