package org.xenoserver.control;

import java.io.FileWriter;
import java.io.IOException;

/**
 * Create a virtual block device.
 */
public class CommandVbdCreatePhysical extends Command {
    /** Virtual disk to map to. */
    private String partition_name;
    /** Domain to create VBD for. */
    private int domain_id;
    /** VBD number to use. */
    private int vbd_num;
    /** Access mode to grant. */
    private Mode mode;

    /**
     * Constructor for CommandVbdCreate.
     * @param partition Partition to map to.
     * @param domain_id Domain to map for.
     * @param vbd_num VBD number within domain.
     * @param mode Access mode to grant.
     */
    public CommandVbdCreatePhysical(
        String partition,
        int domain_id,
        int vbd_num,
        Mode mode) {
        this.partition_name = partition;
        this.domain_id = domain_id;
        this.vbd_num = vbd_num;
        this.mode = mode;
    }

    /**
     * @see org.xenoserver.control.Command#execute()
     */
    public String execute() throws CommandFailedException {
        String resolved = StringPattern.parse(partition_name).resolve(domain_id);
        Partition partition = PartitionManager.IT.getPartition(resolved);
        if (partition == null) {
            throw new CommandFailedException(
                "No partition " + partition_name + " (resolved to " + resolved + ") exists");
        }

        VirtualDisk vd = new VirtualDisk("vbd:" + partition.getName());
        vd.addPartition(partition, partition.getNumSects());

        VirtualBlockDevice vbd =
            new VirtualBlockDevice(vd, domain_id, vbd_num, mode);

        String command = vd.dumpForXen(vbd);

        try {
            FileWriter fw = new FileWriter("/proc/xeno/vhd");
            fw.write(command);
            fw.flush();
            fw.close();
        } catch (IOException e) {
            throw new CommandFailedException(
                "Could not write VBD details to /proc/xeno/vhd",
                e);
        }

        return "Created virtual block device "
            + vbd_num
            + " for domain "
            + domain_id;
    }
}
