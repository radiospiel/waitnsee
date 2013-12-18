1. Install the gem

        gem install waitnsee
        
Make sure you have all preliminaries installed. These include make and cc.

2. waitnsee examples:

Start sshd with a specific configuration and inform it of changes in the config file:

        waitnsee my_sshd.conf:HUP -- /usr/sbin/sshd -c my_sshd.conf -D

Additionally restart sshd if a restart.sshd file was created or touched:

        waitnsee my_sshd.conf:HUP my_sshd.restart:RESTART -- /usr/sbin/sshd -c my_sshd.conf -D

3. waitnsee actions

waitnsee support the following actions:

RESTART: restart the binary
everything else: send specific signal

4. Return code

waitnsee returns with whatever exitcode is returned from the application.
