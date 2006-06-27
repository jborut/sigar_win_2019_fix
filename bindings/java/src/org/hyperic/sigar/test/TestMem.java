package org.hyperic.sigar.test;

import org.hyperic.sigar.Sigar;
import org.hyperic.sigar.Mem;

public class TestMem extends SigarTestCase {

    public TestMem(String name) {
        super(name);
    }

    public void testCreate() throws Exception {
        Sigar sigar = getSigar();

        Mem mem = sigar.getMem();

        assertGtZeroTrace("Total", mem.getTotal());

        assertGtZeroTrace("Used", mem.getUsed());

        assertGtZeroTrace("Free", mem.getFree());

        assertGtZeroTrace("ActualUsed", mem.getUsed());

        assertGtZeroTrace("ActualFree", mem.getFree());

        assertGtZeroTrace("Ram", mem.getRam());

        assertTrue((mem.getRam() % 8) == 0);
    }
}