#!/usr/bin/python
import signal
import os
import sys
import re
from tkinter import *

signal.signal(signal.SIGINT, lambda *args: exit(128 + signal.SIGINT))

class Request:
    def __init__(self, source, group, resource):
        self.source = source
        self.group = group
        self.resource = resource
        self.hint = ""
        self.fields = {}

    def ref(self, refname):
        if refname == "source": return self.source
        if refname == "group": return self.group
        if refname == "resource": return self.resource
        return self.fields[refname]

    def description(self):
        if "description" not in self.fields:
            # default desc not implemented yet
            raise NotImplementedError()

        elems = [ elem.strip() for elem in self.fields["description"].split("$$") ]
        elems = [ re.sub(r'\${([^}]*)}', lambda x: self.ref(x[1]), elem) for elem in elems ]
        return "\n".join(elems)

def parse_req(argv):
    req = Request(argv[1], argv[2], argv[3])

    for i, arg in enumerate(argv):
        if i <= 3: continue

        key, val = arg.split("=", 1)
        req.fields[key] = val

        if key == "hint":
            req.hint = val.strip()

    return req

req = parse_req(sys.argv)

root = Tk()
root.title(f"Permission request from script '{req.source}'")

message = req.description()
Label(root, text=message, anchor="e", justify=LEFT).pack()

frame = Frame(root)
frame.pack()

if req.hint == "future":
    Button(frame, text='Accept and remember', command = lambda: exit(0)).grid(row = 0, column = 0, columnspan = 2, sticky = "EWNS")
    Button(frame, text='Reject this time', command = lambda: exit(3)).grid(row = 1, column = 0, sticky = "EWNS")
    Button(frame, text='Reject and remember', command = lambda: exit(2)).grid(row = 1, column = 1, sticky = "EWNS")
else:
    Button(frame, text='Accept and remember', command = lambda: exit(0)).grid(row = 0, column = 0, sticky = "EWNS")
    Button(frame, text='Reject this time', command = lambda: exit(3)).grid(row = 0, column = 1, sticky = "EWNS")
    Button(frame, text='Accept this time', command = lambda: exit(1)).grid(row = 1, column = 0, sticky = "EWNS")
    Button(frame, text='Reject and remember', command = lambda: exit(2)).grid(row = 1, column = 1, sticky = "EWNS")
root.mainloop()

exit(3)
