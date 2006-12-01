from xen.xend.server.DevController import DevController

from xen.xend.XendError import VmError
import xen.xend
import os

class VfbifController(DevController):
    """Virtual frame buffer controller. Handles all vfb devices for a domain.
    """

    def __init__(self, vm):
        DevController.__init__(self, vm)

    def getDeviceDetails(self, config):
        """@see DevController.getDeviceDetails"""
        devid = 0
        back = {}
        front = {}
        return (devid, back, front)

    def createDevice(self, config):
        DevController.createDevice(self, config)
        std_args = [ "--domid", "%d" % self.vm.getDomid(),
                     "--title", self.vm.getName() ]
        t = config.get("type", None)
        if t == "vnc":
            # Try to start the vnc backend
            args = [xen.util.auxbin.pathTo("xen-vncfb")]
            if config.has_key("vncunused"):
                args += ["--unused"]
            elif config.has_key("vncdisplay"):
                args += ["--vncport", "%d" % (5900 + config["vncdisplay"])]
            vnclisten = config.get("vnclisten",
                                   xen.xend.XendRoot.instance().get_vnclisten_address())
            args += [ "--listen", vnclisten ]
            os.spawnve(os.P_NOWAIT, args[0], args + std_args, os.environ)
        elif t == "sdl":
            args = [xen.util.auxbin.pathTo("xen-sdlfb")]
            env = dict(os.environ)
            if config.has_key("display"):
                env['DISPLAY'] = config["display"]
            if config.has_key("xauthority"):
                env['XAUTHORITY'] = config["xauthority"]
            os.spawnve(os.P_NOWAIT, args[0], args + std_args, env)
        else:
            raise VmError('Unknown vfb type %s (%s)' % (t, repr(config)))

class VkbdifController(DevController):
    """Virtual keyboard controller. Handles all vkbd devices for a domain.
    """

    def getDeviceDetails(self, config):
        """@see DevController.getDeviceDetails"""
        devid = 0
        back = {}
        front = {}
        return (devid, back, front)
