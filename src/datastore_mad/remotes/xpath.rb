#!/usr/bin/env ruby

# -------------------------------------------------------------------------- */
# Copyright 2002-2012, OpenNebula Project Leads (OpenNebula.org)             #
# Licensed under the Apache License, Version 2.0 (the "License"); you may    */
# not use this file except in compliance with the License. You may obtain    */
# a copy of the License at                                                   */
#                                                                            */
# http://www.apache.org/licenses/LICENSE-2.0                                 */
#                                                                            */
# Unless required by applicable law or agreed to in writing, software        */
# distributed under the License is distributed on an "AS IS" BASIS,          */
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   */
# See the License for the specific language governing permissions and        */
# limitations under the License.                                             */
# -------------------------------------------------------------------------- */

#
# Simple command to parse an XML document (b64 encoded) and return one or more
# elements (refer by xpath)
#
require 'base64'
require 'rexml/document'
require 'getoptlong'

opts = opts = GetoptLong.new(
    [ '--base64',  '-b', GetoptLong::REQUIRED_ARGUMENT ]
)

tmp64 = ""

begin
    opts.each do |opt, arg|
        case opt
            when '--base64'
                tmp64 = arg
        end
    end
rescue Exception => e
    exit(-1)
end

values = ""

tmp = Base64::decode64(tmp64)
xml = REXML::Document.new(tmp).root

ARGV.each do |xpath|
	element = xml.elements[xpath.dup]
    values << element.text.to_s if !element.nil?
    values << "\0"
end

puts values

exit 0
