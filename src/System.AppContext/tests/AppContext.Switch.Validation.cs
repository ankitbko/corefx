// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

using System;
using Xunit;

namespace System.Tests
{
    public partial class AppContextTests
    {
        [Fact]
        public void NullSwitchName_EnableSwitch()
        {
            Assert.Throws<ArgumentNullException>("switchName", () => AppContext.SetSwitch(null, true));
        }

        [Fact]
        public void NullSwitchName_DisableSwitch()
        {
            Assert.Throws<ArgumentNullException>("switchName", () => AppContext.SetSwitch(null, false));
        }

        [Fact]
        public void NullSwitchName_TryGetSwitchValue()
        {
            Assert.Throws<ArgumentNullException>("switchName", () =>
            {
                bool output;
                AppContext.TryGetSwitch(null, out output);
            });
        }

        [Fact]
        public void EmptySwitchName_EnableSwitch()
        {
            Assert.Throws<ArgumentException>("switchName", () => AppContext.SetSwitch(string.Empty, true));
        }

        [Fact]
        public void EmptySwitchName_DisableSwitch()
        {
            Assert.Throws<ArgumentException>("switchName", () => AppContext.SetSwitch(string.Empty, false));
        }

        [Fact]
        public void EmptySwitchName_TryGetSwitchValue()
        {
            bool output;
            Assert.Throws<ArgumentException>("switchName", () => AppContext.TryGetSwitch(string.Empty, out output));
        }
    }
}
