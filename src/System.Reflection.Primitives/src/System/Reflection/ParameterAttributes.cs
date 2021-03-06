// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// ParameterAttributes is an enum defining the attributes that may be 
// associated with a Parameter.  These are defined in CorHdr.h.

namespace System.Reflection
{
    // This Enum matchs the CorParamAttr defined in CorHdr.h
    [Flags]
    [System.Runtime.InteropServices.ComVisible(true)]
    public enum ParameterAttributes
    {
        None = 0x0000,      // no flag is specified
        In = 0x0001,     // Param is [In]    
        Out = 0x0002,     // Param is [Out]   
        Lcid = 0x0004,     // Param is [lcid]  

        Retval = 0x0008,     // Param is [Retval]
        Optional = 0x0010,     // Param is optional 

        HasDefault = 0x1000,     // Param has default value.
        HasFieldMarshal = 0x2000,     // Param has FieldMarshal.
    }
}
